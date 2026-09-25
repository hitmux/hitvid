//go:build native && linux && amd64

package main

/*
#cgo CFLAGS: -I${SRCDIR}/native/include -I${SRCDIR}/native/build/prefix/include/chafa -I${SRCDIR}/native/build/prefix/include -I${SRCDIR}/native/build/prefix/lib/chafa/include -I${SRCDIR}/native/build/prefix/lib/glib-2.0/include
#cgo LDFLAGS: -L${SRCDIR}/native/build/prefix/lib -Wl,--start-group -lchafa -lglib-2.0 -lavformat -lavcodec -lswscale -lswresample -lavutil -Wl,--end-group -lpcre2-8 -lffi -lz -latomic -pthread -ldl -lm
#include <stdlib.h>
#include "native/include/media_bridge.h"
*/
import "C"

import (
	"context"
	"fmt"
	"log"
	"sync"
	"unsafe"
)

func nativeBackendAvailable() bool {
	return true
}

type nativeDecoder struct {
	ptr *C.hv_decoder
}

func openNativeDecoder(path string, targetFPS, maxWidth int) (*nativeDecoder, error) {
	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))
	var ptr *C.hv_decoder
	if rc := C.hv_decoder_open(cPath, C.int(targetFPS), C.int(maxWidth), &ptr); rc < 0 {
		return nil, nativeError()
	}
	return &nativeDecoder{ptr: ptr}, nil
}

func nativeError() error {
	return fmt.Errorf("native media: %s", C.GoString(C.hv_last_error()))
}

func (decoder *nativeDecoder) close() {
	if decoder != nil && decoder.ptr != nil {
		C.hv_decoder_close(decoder.ptr)
		decoder.ptr = nil
	}
}

func (decoder *nativeDecoder) seek(seconds float64) error {
	if rc := C.hv_decoder_seek(decoder.ptr, C.double(seconds)); rc < 0 {
		return nativeError()
	}
	return nil
}

func (decoder *nativeDecoder) next() ([]byte, int, int, int, int, error) {
	var frame C.hv_video_frame
	rc := C.hv_decoder_next(decoder.ptr, &frame)
	if rc == 0 {
		return nil, 0, 0, 0, 0, nil
	}
	if rc < 0 {
		return nil, 0, 0, 0, 0, nativeError()
	}
	data := C.GoBytes(unsafe.Pointer(frame.pixels), C.int(frame.size))
	return data, int(frame.width), int(frame.height), int(frame.stride), int(frame.size), nil
}

type nativeRenderer struct {
	ptr *C.hv_renderer
}

func openNativeRenderer() (*nativeRenderer, error) {
	cSymbols := C.CString(symbols)
	cColors := C.CString(colors)
	cDither := C.CString(dither)
	defer C.free(unsafe.Pointer(cSymbols))
	defer C.free(unsafe.Pointer(cColors))
	defer C.free(unsafe.Pointer(cDither))
	var ptr *C.hv_renderer
	if rc := C.hv_renderer_open(C.int(width), C.int(height), cSymbols, cColors, cDither, &ptr); rc < 0 {
		return nil, nativeError()
	}
	return &nativeRenderer{ptr: ptr}, nil
}

func (renderer *nativeRenderer) close() {
	if renderer != nil && renderer.ptr != nil {
		C.hv_renderer_close(renderer.ptr)
		renderer.ptr = nil
	}
}

func (renderer *nativeRenderer) render(pixels []byte, frameWidth, frameHeight, stride int) ([]byte, error) {
	var output *C.char
	var outputSize C.size_t
	if rc := C.hv_renderer_render(renderer.ptr, (*C.uint8_t)(unsafe.Pointer(&pixels[0])), C.int(frameWidth), C.int(frameHeight), C.int(stride), &output, &outputSize); rc < 0 {
		return nil, nativeError()
	}
	defer C.hv_renderer_free_output(output)
	return C.GoBytes(unsafe.Pointer(output), C.int(outputSize)), nil
}

type nativeRenderJob struct {
	index       int
	pixels      []byte
	frameWidth  int
	frameHeight int
	frameStride int
}

func playVideoNative(ctx context.Context, path string, startFrame int) string {
	stateMutex.Lock()
	isPaused = false
	currentFrameIndex = startFrame
	totalFrames = 0
	extractionComplete = false
	renderingComplete = false
	frameStore := newRenderedFrameStore()
	userAction = ""
	stateMutex.Unlock()

	decoder, err := openNativeDecoder(path, fps, width*8)
	if err != nil {
		log.Printf("%v\r\n", err)
		return "finished"
	}
	defer decoder.close()
	duration := float64(C.hv_decoder_duration(decoder.ptr))
	stateMutex.Lock()
	totalFrames = int(duration * float64(fps))
	if totalFrames > 0 && currentFrameIndex >= totalFrames {
		currentFrameIndex = totalFrames - 1
		startFrame = currentFrameIndex
	}
	stateMutex.Unlock()
	if startFrame > 0 {
		if err := decoder.seek(float64(startFrame) / float64(fps)); err != nil {
			log.Printf("%v\r\n", err)
			return "finished"
		}
	}

	sessionCtx, sessionCancel := context.WithCancel(ctx)
	defer sessionCancel()
	jobs := make(chan nativeRenderJob, numThreads*2)
	frameSlots := make(chan struct{}, renderedFrameCapacity)
	var workers sync.WaitGroup
	for i := 0; i < numThreads; i++ {
		workers.Add(1)
		go func() {
			defer workers.Done()
			renderer, renderErr := openNativeRenderer()
			if renderErr != nil {
				log.Printf("%v\r\n", renderErr)
			}
			if renderer != nil {
				defer renderer.close()
			}
			for job := range jobs {
				var output []byte
				if renderer != nil {
					output, renderErr = renderer.render(job.pixels, job.frameWidth, job.frameHeight, job.frameStride)
				}
				if renderErr != nil && sessionCtx.Err() == nil {
					log.Printf("native Chafa frame %d failed: %v\r\n", job.index, renderErr)
				}
				stateMutex.Lock()
				frameStore.put(job.index, output)
				frameReadyCond.Broadcast()
				stateMutex.Unlock()
			}
		}()
	}

	decodeDone := make(chan struct{})
	go func() {
		defer close(decodeDone)
		defer close(jobs)
		nextFrame := startFrame
		for sessionCtx.Err() == nil {
			pixels, frameWidth, frameHeight, frameStride, _, nextErr := decoder.next()
			if nextErr != nil {
				if sessionCtx.Err() == nil {
					log.Printf("%v\r\n", nextErr)
				}
				break
			}
			if pixels == nil {
				break
			}
			select {
			case frameSlots <- struct{}{}:
			case <-sessionCtx.Done():
				return
			}
			select {
			case jobs <- nativeRenderJob{index: nextFrame, pixels: pixels, frameWidth: frameWidth, frameHeight: frameHeight, frameStride: frameStride}:
				nextFrame++
			case <-sessionCtx.Done():
				<-frameSlots
				return
			}
		}
		stateMutex.Lock()
		extractionComplete = true
		frameReadyCond.Broadcast()
		stateMutex.Unlock()
	}()
	renderDone := make(chan struct{})
	go func() {
		defer close(renderDone)
		<-decodeDone
		workers.Wait()
		stateMutex.Lock()
		renderingComplete = true
		frameReadyCond.Broadcast()
		stateMutex.Unlock()
	}()

	playbackLoop(sessionCtx, frameStore, frameSlots)
	sessionCancel()
	<-decodeDone
	<-renderDone
	stateMutex.Lock()
	defer stateMutex.Unlock()
	if userAction != "" {
		return userAction
	}
	return "finished"
}
