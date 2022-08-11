package api

import (
	"encoding/json"
	"net/http"
	"strings"

	"github.com/gin-gonic/gin"
	"github.com/gin-gonic/gin/binding"

	"github.com/equinor/vds-slice/internal/vds"
)

type FenceQuery struct {
	Vds              string      `json:"vds"               binding:"required"`
	CoordinateSystem string      `json:"coordinate_system" binding:"required"`
	Fence            [][]float32 `json:"coordinates"       binding:"required"`
	Sas              string      `json:"sas"               binding:"required"`
}

type SliceQuery struct {
	Vds       string `json:"vds"       binding:"required"`
	Direction string `json:"direction" binding:"required"`
	Lineno    *int   `json:"lineno"    binding:"required"`
	Sas       string `json:"sas"       binding:"required"`
}

type Endpoint struct {
	StorageURL string
	Protocol   string
}

func (e *Endpoint) slice(ctx *gin.Context, query SliceQuery) {
	conn, err := vds.MakeConnection(e.Protocol, e.StorageURL, query.Vds, query.Sas)
	if err != nil {
		ctx.AbortWithError(http.StatusBadRequest, err)
		return
	}

	axis, err := vds.GetAxis(strings.ToLower(query.Direction))
	if err != nil {
		ctx.AbortWithError(http.StatusBadRequest, err)
		return
	}

	metadata, err := vds.SliceMetadata(*conn, *query.Lineno, axis)
	if err != nil {
		ctx.AbortWithError(http.StatusInternalServerError, err)
		return
	}

	data, err := vds.Slice(*conn, *query.Lineno, axis)
	if err != nil {
		ctx.AbortWithError(http.StatusInternalServerError, err)
		return
	}
	writeResponse(ctx, metadata, data)
}

func sliceParseGetReq(ctx *gin.Context) (*SliceQuery, error) {
	var query SliceQuery

	err := json.Unmarshal([]byte(ctx.Query("query")), &query)
	if err != nil {
		return nil, err
	}

	if err = binding.Validator.ValidateStruct(&query); err != nil {
		return nil, err
	}

	return &query, nil
}

func (e *Endpoint) Health(ctx *gin.Context) {
	ctx.String(http.StatusOK, "I am up and running")
}

func (e *Endpoint) SliceGet(ctx *gin.Context) {
	query, err := sliceParseGetReq(ctx)
	if err != nil {
		ctx.AbortWithError(http.StatusBadRequest, err)
		return
	}
	e.slice(ctx, *query)
}

func (e *Endpoint) SlicePost(ctx *gin.Context) {
	var query SliceQuery
	if err := ctx.ShouldBind(&query); err != nil {
		ctx.AbortWithError(http.StatusBadRequest, err)
		return
	}
	e.slice(ctx, query)
}

func fenceParseGetReq(ctx *gin.Context) (*FenceQuery, error) {
	var query FenceQuery

	err := json.Unmarshal([]byte(ctx.Query("query")), &query)
	if err != nil {
		return nil, err
	}

	if err = binding.Validator.ValidateStruct(&query); err != nil {
		return nil, err
	}

	return &query, nil
}

func (e *Endpoint) FenceGet(ctx *gin.Context) {
	query, err := fenceParseGetReq(ctx)
	if err != nil {
		ctx.AbortWithError(http.StatusBadRequest, err)
		return
	}
	e.fence(ctx, *query)
}

func (e *Endpoint) FencePost(ctx *gin.Context) {
	var query FenceQuery
	if err := ctx.ShouldBind(&query); err != nil {
		ctx.AbortWithError(http.StatusBadRequest, err)
		return
	}
	e.fence(ctx, query)
}

func (e *Endpoint) fence(ctx *gin.Context, query FenceQuery) {
	conn, err := vds.MakeConnection(e.Protocol, e.StorageURL, query.Vds, query.Sas)
	if err != nil {
		ctx.AbortWithError(http.StatusBadRequest, err)
		return
	}

	data, err := vds.Fence(*conn, query.CoordinateSystem, query.Fence)
	if err != nil {
		ctx.AbortWithError(http.StatusInternalServerError, err)
		return
	}
	writeResponse(ctx, []byte{}, data)
}
