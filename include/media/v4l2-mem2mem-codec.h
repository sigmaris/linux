/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Memory-to-memory codec framework for Video for Linux 2.
 *
 * Helper functions for codec devices that use memory buffers for both source
 * and destination.
 *
 * Copyright (c) 2019 Collabora Ltd.
 *
 * Author:
 *	Boris Brezillon <boris.brezillon@collabora.com>
 */

#ifndef _MEDIA_V4L2_MEM2MEM_CODEC_H
#define _MEDIA_V4L2_MEM2MEM_CODEC_H

#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-fh.h>
#include <media/v4l2-mem2mem.h>

struct v4l2_m2m_codec_ctx;

/**
 * struct v4l2_m2m_codec_ctrl_desc - Codec control description
 * @per_request: set to true if the control is expected to be set everytime a
 *		 decoding/encoding request is queued
 * @mandatory: set to true if the control is mandatory
 * @cfg: control configuration
 */
struct v4l2_m2m_codec_ctrl_desc {
	u32 per_request : 1;
	u32 mandatory : 1;
	struct v4l2_ctrl_config cfg;
};

/**
 * struct v4l2_m2m_codec_ctrls - Codec controls
 * @ctrls: array of control descriptions
 * @num_ctrls: size of the ctrls array
 *
 * Structure used to declare codec specific controls.
 */
struct v4l2_m2m_codec_ctrls {
	const struct v4l2_m2m_codec_ctrl_desc *ctrls;
	unsigned int num_ctrls;
};

#define V4L2_M2M_CODEC_CTRLS(_name, ...)						\
	struct v4l2_m2m_codec_ctrls _name = {						\
		.ctrls = (const struct v4l2_m2m_codec_ctrl_desc[]){__VA_ARGS__},	\
		.num_ctrls = sizeof((struct v4l2_m2m_codec_ctrl_desc[]){__VA_ARGS__}) /	\
			     sizeof(struct v4l2_m2m_codec_ctrl_desc),			\
	}

/**
 * struct v4l2_m2m_codec_decoded_fmt_desc - Decoded format description
 * @fourcc: the 4CC code of the decoded format
 * @priv: driver private data to associate to this decoded format
 *
 * Structure used to describe decoded formats.
 */
struct v4l2_m2m_codec_decoded_fmt_desc {
	u32 fourcc;
	const void *priv;
};

/**
 * struct v4l2_m2m_codec_coded_fmt_ops - Coded format methods
 * @adjust_fmt: adjust a coded format before passing it back to userspace.
 *		Particularly useful when one wants to tweak any of the
 *		params set by the core (sizeimage, width, height, ...)
 * @start: called when vb2_ops->start_streaming() is called. Any coded-format
 *	   specific context initialization should happen here
 * @stop: called when vb2_ops->stop_streaming() is called. Any coded-format
 *	  specific context cleanup should happen here
 * @run: called when v4l2_m2m_ops->device_run() is called. This method should
 *	 issue the encoding/decoding request
 */
struct v4l2_m2m_codec_coded_fmt_ops {
	int (*adjust_fmt)(struct v4l2_m2m_codec_ctx *ctx,
			  struct v4l2_format *f);
	int (*start)(struct v4l2_m2m_codec_ctx *ctx);
	void (*stop)(struct v4l2_m2m_codec_ctx *ctx);
	int (*run)(struct v4l2_m2m_codec_ctx *ctx);
};

/**
 * struct v4l2_m2m_codec_coded_fmt_desc - Coded format description
 * @fourcc: 4CC code describing this coded format
 * @requires_request: set to true if the codec requires a media request object
 *		      to process encoding/decoding requests
 * @frmsize: frame size constraint. Can be NULL if the codec does not have any
 *	     alignment/min/max size constraints for this coded format
 * @ctrls: controls attached to this coded format
 * @ops: coded format ops
 * @priv: driver private data
 */
struct v4l2_m2m_codec_coded_fmt_desc {
	u32 fourcc;
	u32 requires_requests : 1;
	const struct v4l2_frmsize_stepwise *frmsize;
	const struct v4l2_m2m_codec_ctrls *ctrls;
	const struct v4l2_m2m_codec_coded_fmt_ops *ops;
	const void *priv;
};

#define V4L2_M2M_CODEC_CODED_FMTS(_fmt_array)		\
	.num_coded_fmts = ARRAY_SIZE(_fmt_array),	\
	.coded_fmts = _fmt_array

#define V4L2_M2M_CODEC_DECODED_FMTS(_fmt_array)		\
	.num_decoded_fmts = ARRAY_SIZE(_fmt_array),	\
	.decoded_fmts = _fmt_array

/**
 * struct v4l2_m2m_codec_caps - Codec capabilities
 * @coded_fmts: array of supported coded formats
 * @num_coded_fmts: size of the coded_fmts array
 * @decoded_fmts: array of supported decoded formats
 * @num_decoded_fmts: size of the decoded_fmts array
 *
 * This structure is describing the formats supported by the codec.
 */
struct v4l2_m2m_codec_caps {
	const struct v4l2_m2m_codec_coded_fmt_desc *coded_fmts;
	unsigned int num_coded_fmts;
	const struct v4l2_m2m_codec_decoded_fmt_desc *decoded_fmts;
	unsigned int num_decoded_fmts;
};

/**
 * enum v4l2_m2m_codec_type - Codec type
 * @V4L2_M2M_ENCODER: encoder
 * @V4L2_M2M_DECODER: decoder
 */
enum v4l2_m2m_codec_type {
	V4L2_M2M_ENCODER,
	V4L2_M2M_DECODER,
};

/**
 * struct v4l2_m2m_codec_ops - Codec methods
 * @queue_init: called by the v4l2_m2m_codec_queue_init() helper to let the
 *		driver initialize the src/dst queues
 */
struct v4l2_m2m_codec_ops {
	int (*queue_init)(struct v4l2_m2m_codec_ctx *ctx,
			  struct vb2_queue *src_vq,
			  struct vb2_queue *dst_vq);
};

/**
 * struct v4l2_m2m_codec - Codec object
 * @vdev: video device exposed by the codec
 * @type: type of codec
 * @m2m_dev: m2m device this codec is attached to
 * @caps: codec capabilities
 * @ops: codec operations
 */
struct v4l2_m2m_codec {
	struct video_device vdev;
	enum v4l2_m2m_codec_type type;
	struct v4l2_m2m_dev *m2m_dev;
	const struct v4l2_m2m_codec_caps *caps;
	const struct v4l2_m2m_codec_ops *ops;
};

static inline struct v4l2_m2m_codec *
vdev_to_v4l2_m2m_codec(struct video_device *vdev)
{
	return container_of(vdev, struct v4l2_m2m_codec, vdev);
}

static inline struct video_device *
v4l2_m2m_codec_to_vdev(struct v4l2_m2m_codec *codec)
{
	return &codec->vdev;
}

static inline enum v4l2_m2m_codec_type
v4l2_m2m_codec_get_type(const struct v4l2_m2m_codec *codec)
{
	return codec->type;
}

/**
 * struct v4l2_m2m_codec_ctx - Codec context
 * @fh: file handle
 * @coded_fmt: current coded format
 * @decoded_fmt: current decoded format
 * @coded_fmt_desc: current coded format desc
 * @decoded_fmt_desc: current decoded format desc
 * @ctrl_hdl: control handler
 * @codec: the codec that has created this context
 */
struct v4l2_m2m_codec_ctx {
	struct v4l2_fh fh;
	struct v4l2_format coded_fmt;
	struct v4l2_format decoded_fmt;
	const struct v4l2_m2m_codec_coded_fmt_desc *coded_fmt_desc;
	const struct v4l2_m2m_codec_decoded_fmt_desc *decoded_fmt_desc;
	struct v4l2_ctrl_handler ctrl_hdl;
	struct v4l2_m2m_codec *codec;
};

static inline struct v4l2_m2m_codec_ctx *
fh_to_v4l2_m2m_codec_ctx(struct v4l2_fh *fh)
{
	return container_of(fh, struct v4l2_m2m_codec_ctx, fh);
}

static inline struct v4l2_m2m_codec_ctx *
file_to_v4l2_m2m_codec_ctx(struct file *file)
{
	return fh_to_v4l2_m2m_codec_ctx(file->private_data);
}

static inline struct v4l2_m2m_ctx *
v4l2_m2m_codec_get_m2m_ctx(struct v4l2_m2m_codec_ctx *ctx)
{
	return ctx->fh.m2m_ctx;
}

static inline struct v4l2_ctrl_handler *
v4l2_m2m_codec_get_ctrl_handler(struct v4l2_m2m_codec_ctx *ctx)
{
	return &ctx->ctrl_hdl;
}

struct v4l2_m2m_codec_run {
	struct {
		struct vb2_v4l2_buffer *src;
		struct vb2_v4l2_buffer *dst;
	} bufs;
};

int v4l2_m2m_codec_init(struct v4l2_m2m_codec *codec,
			enum v4l2_m2m_codec_type type,
			struct v4l2_m2m_dev *m2m_dev,
			struct v4l2_device *v4l2_dev,
			const struct v4l2_m2m_codec_caps *caps,
			const struct v4l2_m2m_codec_ops *ops,
			const struct v4l2_file_operations *vdev_fops,
			const struct v4l2_ioctl_ops *vdev_ioctl_ops,
			struct mutex *lock, const char *name, void *drvdata);
int v4l2_m2m_codec_ctx_init(struct v4l2_m2m_codec_ctx *ctx, struct file *file,
			    struct v4l2_m2m_codec *codec);
void v4l2_m2m_codec_ctx_cleanup(struct v4l2_m2m_codec_ctx *ctx);
void v4l2_m2m_codec_run_preamble(struct v4l2_m2m_codec_ctx *ctx,
				 struct v4l2_m2m_codec_run *run);
void v4l2_m2m_codec_run_postamble(struct v4l2_m2m_codec_ctx *ctx,
				  struct v4l2_m2m_codec_run *run);
void v4l2_m2m_codec_job_finish(struct v4l2_m2m_codec_ctx *ctx,
			       enum vb2_buffer_state state);

static inline const struct v4l2_format *
v4l2_m2m_codec_get_coded_fmt(struct v4l2_m2m_codec_ctx *ctx)
{
	return &ctx->coded_fmt;
}

static inline const struct v4l2_m2m_codec_coded_fmt_desc *
v4l2_m2m_codec_get_coded_fmt_desc(struct v4l2_m2m_codec_ctx *ctx)
{
	return ctx->coded_fmt_desc;
}

static inline const struct v4l2_format *
v4l2_m2m_codec_get_decoded_fmt(struct v4l2_m2m_codec_ctx *ctx)
{
	return &ctx->decoded_fmt;
}

static inline const struct v4l2_m2m_codec_decoded_fmt_desc *
v4l2_m2m_codec_get_decoded_fmt_desc(struct v4l2_m2m_codec_ctx *ctx)
{
	return ctx->decoded_fmt_desc;
}

void v4l2_m2m_codec_reset_decoded_fmt(struct v4l2_m2m_codec_ctx *ctx);
const struct v4l2_m2m_codec_coded_fmt_desc *
v4l2_m2m_codec_find_coded_fmt_desc(struct v4l2_m2m_codec *codec, u32 fourcc);
int v4l2_m2m_codec_enum_framesizes(struct file *file, void *priv,
				   struct v4l2_frmsizeenum *fsize);
int v4l2_m2m_codec_enum_output_fmt(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f);
int v4l2_m2m_codec_enum_capture_fmt(struct file *file, void *priv,
				    struct v4l2_fmtdesc *f);
int v4l2_m2m_codec_g_output_fmt(struct file *file, void *priv,
				struct v4l2_format *f);
int v4l2_m2m_codec_g_capture_fmt(struct file *file, void *priv,
				 struct v4l2_format *f);
int v4l2_m2m_codec_try_output_fmt(struct file *file, void *priv,
				  struct v4l2_format *f);
int v4l2_m2m_codec_try_capture_fmt(struct file *file, void *priv,
				   struct v4l2_format *f);
int v4l2_m2m_codec_s_output_fmt(struct file *file, void *priv,
				struct v4l2_format *f);
int v4l2_m2m_codec_s_capture_fmt(struct file *file, void *priv,
				 struct v4l2_format *f);

int v4l2_m2m_codec_queue_setup(struct vb2_queue *vq, unsigned int *num_buffers,
			       unsigned int *num_planes, unsigned int sizes[],
			       struct device *alloc_devs[]);
void v4l2_m2m_codec_queue_cleanup(struct vb2_queue *vq, u32 state);
int v4l2_m2m_codec_buf_out_validate(struct vb2_buffer *vb);
int v4l2_m2m_codec_buf_prepare(struct vb2_buffer *vb);
void v4l2_m2m_codec_buf_queue(struct vb2_buffer *vb);
void v4l2_m2m_codec_buf_request_complete(struct vb2_buffer *vb);
int v4l2_m2m_codec_start_streaming(struct vb2_queue *q, unsigned int count);
void v4l2_m2m_codec_stop_streaming(struct vb2_queue *q);

int v4l2_m2m_codec_request_validate(struct media_request *req);
int v4l2_m2m_codec_device_run(struct v4l2_m2m_codec_ctx *ctx);

#endif /* _MEDIA_V4L2_MEM2MEM_CODEC_H */
