package grpc

import (
	"context"
	"log"
	"math"
	"time"

	"github.com/equinor/vds-slice/internal/core"

	"github.com/google/uuid"

)

func min(a, b int) int {
	if a < b {
		return a
	}
	return b
}

func logmsg(info *Info, msg string) {
	log.Printf("[PID: %v][Part: %v/%v] %v", info.Pid, info.Part, info.Parts, msg)
}

func MakePID() string {
	return uuid.New().String()
}

func newFenceRequest(
	pid string,
	part int32,
	parts int32,
	url string,
	sas string,
	interpolation int32,
	coordinateSystem int32,
	coordinates [][]float32,
) *FenceRequest {
	pbCoordinates := make([]*Coordinate, len(coordinates))
	for i, coordinate := range coordinates {
		pbCoordinates[i] = &Coordinate{ 
			X: coordinate[0],
			Y: coordinate[1],
		}
	}

	return &FenceRequest{ 
		Info: &Info{
			Pid:  pid,
			Part: part,
			Parts: parts,
		},
		Connection: &Connection{
			Url: url,
			Credential: sas,
		},
		CoordinateSystem: coordinateSystem,
		Interpolation:    interpolation,
		Coordinates:      pbCoordinates,
	}
}

/* Our gRPC client
 * 
 * The scheduler is responsible for splitting user-requests into smaller
 * sub-request and distributing these to worker nodes (fan-out). Sub-request
 * responses from the workers are then stitched together and returned to the
 * caller. I.e. the distribution of the request is opaque to the user of the
 * Scheduler.
 * 
 */  
type Scheduler struct {
	// The OneseismicClient is auto-generated by protoc
	client OneseismicClient
	jobs   int
}

func (g * Scheduler) sendFenceSubRequest(
	request *FenceRequest,
	errors chan error ,
	responses chan *FenceResponse,
) {
	logmsg(request.Info, "sent request to worker...")
	ctx, cancel := context.WithTimeout(context.Background(), 5 * time.Minute)
	defer cancel()
	resp, err := g.client.GetFence(ctx, request)
	if err != nil {
		errors <- err
	} else {
		responses <- resp
	}
}

func (g *Scheduler) Fence(
	url string,
	sas string,
	interpolation int32,
	coordinateSystem int32,
	coordinates [][]float32,
	shape core.CubeShape,
) ([]byte, error) {
	pid := MakePID()

	/** Number of parts / splitting strategy
	 *
	 * Currently the request (i.e. the coordinate list) is divided into 4
	 * equally large parts. This is in many ways suboptimal.
	 *
	 * Firstly, the number 4 is chosen at random. Finding a one-fit all number
	 * here is unlikely and we woudl probably benefit from being a function of
     * the request size.
     *
	 * Secondly, splitting into even pieces means there is a great likelyhood
	 * that multiple workers end up fetching the same chunks. Too avoid this we
	 * need a better splitting strategy, based on the chunk layout. I.e. what
	 * we did in oneseismic.
	 */
	var nParts = g.jobs

	errors    := make(chan error,  nParts)
	responses := make(chan *FenceResponse, nParts) 
	
	nTraces := len(coordinates)
	nTracesPerPart := int(math.Ceil(float64(nTraces) / float64(nParts)))

	partSize := nTracesPerPart * shape.Samples * 4

	// Split request into sub-request and send them of one by one in seperate goroutines
	remaining := nTraces;
	from := 0
	partN := 0
	for remaining > 0 {
		size := min(nTracesPerPart, remaining)
		to := from + size

		req := newFenceRequest(
			pid,
			int32(partN),
			int32(nParts - 1), // parts are zero indexed
			url,
			sas,
			interpolation,
			coordinateSystem,
			coordinates[from:to],
		)

		go g.sendFenceSubRequest(
			req,
			errors,
			responses,
		)

		from = to
		remaining -= size
		partN++
	}

	// Collect all responses and copy into output buffer
	out := make([]byte, nTraces * shape.Samples * 4)
	for i := 0; i < nParts; i++ { 
		select {
		case err := <-errors: return nil, err
		case resp := <-responses: 
			logmsg(resp.Info, "collecting response from worker...")
			start := resp.Info.Part * int32(partSize)
			stop  := int(start) + len(resp.Fence)
			copy(out[start : stop], resp.Fence)
		}
	}

	return out, nil
}

func NewScheduler(client OneseismicClient, jobs int) *Scheduler {
	return &Scheduler{ client: client, jobs: jobs}
}
