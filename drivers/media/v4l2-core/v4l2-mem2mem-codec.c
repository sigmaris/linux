// SPDX-License-Identifier: GPL-2.0+
/*
 * Memory-to-memory codec framework for Video for Linux 2.
 *
 * Helper functions for codec devices that use memory buffers for both source
 * and destination.
 *
 * Copyright (c) 2019 Collabora Ltd.
 *
 * Author:
 *      Boris Brezillon <boris.brezillon@collabora.com>
 */

#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem-codec.h>

/**
 * v4l2_m2m_codec_init() - Initializes a v4l2_m2m_codec object
 * @codec: the codec to initialize
 * @type: the type of codec (encoder or decoder)
 * @m2m_dev: the M2M device backing this codec
 * @v4l2_dev: the V4L2 device backing this codec
 * @caps: the codec format capabilities
 * @ops: codec operations
 * @fops: video device file operations
 * @ioctl_ops: video device ioctl operations
 * @lock: the lock to attach to the video device
 * @name: the name of the video device
 * @drvdata: driver private data to attach to the video device
 *
 * This function initializes the codec object and the video device it contains.
 * The caller is still responsible for registering the video dev.
 *
 * Return: 0 in case of success, an negative error code otherwise.
 */
int v4l2_m2m_codec_init(struct v4l2_m2m_codec *codec,
			enum v4l2_m2m_codec_type type,
			struct v4l2_m2m_dev *m2m_dev,
			struct v4l2_device *v4l2_dev,
			const struct v4l2_m2m_codec_caps *caps,
			const struct v4l2_m2m_codec_ops *ops,
			const struct v4l2_file_operations *fops,
			const struct v4l2_ioctl_ops *ioctl_ops,
			struct mutex *lock, const char *name, void *drvdata)
{
	struct video_device *vdev = v4l2_m2m_codec_to_vdev(codec);
	unsigned int i;
	ssize_t ret;

	if (!codec || !caps || !m2m_dev || !ops ||
	    !caps->num_coded_fmts || !caps->num_decoded_fmts ||
	    !caps->coded_fmts || !caps->decoded_fmts || !ops->queue_init)
		return -EINVAL;

	for (i = 0; i < caps->num_coded_fmts; i++) {
		if (!caps->coded_fmts[i].ops)
			return -EINVAL;
	}

	codec->type = type;
	codec->m2m_dev = m2m_dev;
	codec->caps = caps;
	codec->ops = ops;
	vdev->lock = lock;
	vdev->v4l2_dev = v4l2_dev;
	vdev->fops = fops;
	vdev->release = video_device_release_empty;
	vdev->vfl_dir = VFL_DIR_M2M;
	vdev->device_caps = V4L2_CAP_STREAMING;
	vdev->ioctl_ops = ioctl_ops;
	video_set_drvdata(vdev, drvdata);

	if (ioctl_ops->vidioc_g_fmt_vid_out_mplane)
		vdev->device_caps |= V4L2_CAP_VIDEO_M2M_MPLANE;
	else
		vdev->device_caps |= V4L2_CAP_VIDEO_M2M;

	ret = strscpy(vdev->name, name, sizeof(vdev->name));
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_init);

static int v4l2_m2m_codec_add_ctrls(struct v4l2_m2m_codec_ctx *ctx,
				    const struct v4l2_m2m_codec_ctrls *ctrls)
{
	unsigned int i;

	if (!ctrls)
		return 0;

	if (ctrls->num_ctrls && !ctrls->ctrls)
		return -EINVAL;

	for (i = 0; i < ctrls->num_ctrls; i++) {
		const struct v4l2_ctrl_config *cfg = &ctrls->ctrls[i].cfg;

		v4l2_ctrl_new_custom(&ctx->ctrl_hdl, cfg, ctx);
		if (ctx->ctrl_hdl.error)
			return ctx->ctrl_hdl.error;
	}

	return 0;
}

static void v4l2_m2m_codec_cleanup_ctrls(struct v4l2_m2m_codec_ctx *ctx)
{
	v4l2_ctrl_handler_free(&ctx->ctrl_hdl);
}

static int v4l2_m2m_codec_init_ctrls(struct v4l2_m2m_codec_ctx *ctx)
{
	const struct v4l2_m2m_codec_coded_fmt_desc *fmts;
	unsigned int i, nfmts, nctrls = 0;
	int ret;

	fmts = ctx->codec->caps->coded_fmts;
	nfmts = ctx->codec->caps->num_coded_fmts;
	for (i = 0; i < nfmts; i++)
		nctrls += fmts[i].ctrls->num_ctrls;

	v4l2_ctrl_handler_init(&ctx->ctrl_hdl, nctrls);

	for (i = 0; i < nfmts; i++) {
		ret = v4l2_m2m_codec_add_ctrls(ctx, fmts[i].ctrls);
		if (ret)
			goto err_free_handler;
	}

	ret = v4l2_ctrl_handler_setup(&ctx->ctrl_hdl);
	if (ret)
		goto err_free_handler;

	ctx->fh.ctrl_handler = &ctx->ctrl_hdl;
	return 0;

err_free_handler:
	v4l2_ctrl_handler_free(&ctx->ctrl_hdl);
	return ret;
}

static void v4l2_m2m_codec_reset_fmt(struct v4l2_m2m_codec_ctx *ctx,
				     struct v4l2_format *f, u32 fourcc)
{
	const struct v4l2_ioctl_ops *ops = ctx->codec->vdev.ioctl_ops;

	memset(f, 0, sizeof(*f));

	if (ops->vidioc_g_fmt_vid_cap_mplane) {
		f->fmt.pix_mp.pixelformat = fourcc;
	        f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	        f->fmt.pix_mp.colorspace = V4L2_COLORSPACE_JPEG,
	        f->fmt.pix_mp.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	        f->fmt.pix_mp.quantization = V4L2_QUANTIZATION_DEFAULT;
	        f->fmt.pix_mp.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	} else {
		f->fmt.pix.pixelformat = fourcc;
	        f->fmt.pix.field = V4L2_FIELD_NONE;
	        f->fmt.pix.colorspace = V4L2_COLORSPACE_JPEG,
	        f->fmt.pix.ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	        f->fmt.pix.quantization = V4L2_QUANTIZATION_DEFAULT;
	        f->fmt.pix.xfer_func = V4L2_XFER_FUNC_DEFAULT;
	}
}

static void v4l2_m2m_codec_reset_coded_fmt(struct v4l2_m2m_codec_ctx *ctx)
{
	struct v4l2_m2m_codec *codec = ctx->codec;
	const struct v4l2_ioctl_ops *ops = codec->vdev.ioctl_ops;
	const struct v4l2_m2m_codec_coded_fmt_desc *desc;
	struct v4l2_format *f = &ctx->coded_fmt;

	desc = &ctx->codec->caps->coded_fmts[0];
	ctx->coded_fmt_desc = desc;
	v4l2_m2m_codec_reset_fmt(ctx, f, desc->fourcc);

	if (ops->vidioc_g_fmt_vid_cap_mplane) {
		struct v4l2_pix_format_mplane *fmt = &f->fmt.pix_mp;

		if (codec->type == V4L2_M2M_DECODER)
			f->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		else
			f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

		if (desc->frmsize) {
			fmt->width = desc->frmsize->min_width;
			fmt->height = desc->frmsize->min_height;
		}

	} else {
		struct v4l2_pix_format *fmt = &f->fmt.pix;

		if (codec->type == V4L2_M2M_DECODER)
			f->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;
		else
			f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

		if (desc->frmsize) {
			fmt->width = desc->frmsize->min_width;
			fmt->height = desc->frmsize->min_height;
		}
	}

	if (desc->ops->adjust_fmt)
		desc->ops->adjust_fmt(ctx, &ctx->coded_fmt);
}

/**
 * v4l2_m2m_codec_reset_decoded_fmt() - Reset the decoded format embedded in a
 *					codec context
 * @ctx: the context to reset the fmt on
 *
 * The decoded format might need to be reset when specific operations (like
 * updating the format) are done on the coded end of the pipeline. This
 * function is also called at context initialization time.
 */
void v4l2_m2m_codec_reset_decoded_fmt(struct v4l2_m2m_codec_ctx *ctx)
{
	struct v4l2_m2m_codec *codec = ctx->codec;
	const struct v4l2_ioctl_ops *ops = codec->vdev.ioctl_ops;
	const struct v4l2_m2m_codec_coded_fmt_desc *coded_desc;
	struct v4l2_format *f = &ctx->decoded_fmt;

	if (!ctx->coded_fmt_desc)
		v4l2_m2m_codec_reset_coded_fmt(ctx);

	coded_desc = ctx->coded_fmt_desc;
	v4l2_m2m_codec_reset_fmt(ctx, f, codec->caps->decoded_fmts[0].fourcc);
	if (ops->vidioc_g_fmt_vid_cap_mplane) {
		struct v4l2_pix_format_mplane *fmt = &f->fmt.pix_mp;

		if (codec->type == V4L2_M2M_DECODER)
			f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		else
			f->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

		if (coded_desc->frmsize) {
			fmt->width = coded_desc->frmsize->min_width;
			fmt->height = coded_desc->frmsize->min_height;
		}

		v4l2_fill_pixfmt_mp(fmt, fmt->pixelformat,
				    fmt->width, fmt->height);
	} else {
		struct v4l2_pix_format *fmt = &f->fmt.pix;

		if (codec->type == V4L2_M2M_DECODER)
			f->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		else
			f->type = V4L2_BUF_TYPE_VIDEO_OUTPUT;

		if (coded_desc->frmsize) {
			fmt->width = coded_desc->frmsize->min_width;
			fmt->height = coded_desc->frmsize->min_height;
		}

		v4l2_fill_pixfmt(fmt, fmt->pixelformat,
				 fmt->width, fmt->height);
	}

	ctx->decoded_fmt_desc = &codec->caps->decoded_fmts[0];
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_reset_decoded_fmt);

static int v4l2_m2m_codec_queue_init(void *priv, struct vb2_queue *src_vq,
				     struct vb2_queue *dst_vq)
{
	struct v4l2_m2m_codec_ctx *ctx = priv;

	return ctx->codec->ops->queue_init(ctx, src_vq, dst_vq);
}

/**
 * v4l2_m2m_codec_ctx_init() - Initialize a codec context
 * @ctx: the context to initialize
 * @file: the file instance to attach this context to
 * @codec: the codec device creating this context
 *
 * Initializes a codec ctx. A new m2m context is created and the file handle
 * embedded in the codec context is initialized too. We also reset the coded
 * and decoded formats to start from a known state, and add the controls that
 * are defined in the supported coded formats.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int v4l2_m2m_codec_ctx_init(struct v4l2_m2m_codec_ctx *ctx, struct file *file,
			    struct v4l2_m2m_codec *codec)
{
	int ret;

	v4l2_fh_init(&ctx->fh, video_devdata(file));
	ctx->codec = codec;
	ret = v4l2_m2m_codec_init_ctrls(ctx);
	if (ret)
		return ret;

	ctx->fh.m2m_ctx = v4l2_m2m_ctx_init(codec->m2m_dev, ctx,
					    v4l2_m2m_codec_queue_init);
	if (IS_ERR(ctx->fh.m2m_ctx)) {
		ret = PTR_ERR(ctx->fh.m2m_ctx);
		goto err_cleanup_ctrls;
	}

	file->private_data = &ctx->fh;
	v4l2_fh_add(&ctx->fh);

	v4l2_m2m_codec_reset_coded_fmt(ctx);
	v4l2_m2m_codec_reset_decoded_fmt(ctx);
	return 0;

err_cleanup_ctrls:
	v4l2_m2m_codec_cleanup_ctrls(ctx);
	return ret;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_ctx_init);

/**
 * v4l2_m2m_codec_ctx_cleanup() - Clean the codec context up
 * @ctx: codec context to cleanup
 *
 * Undoes what's been done in v4l2_m2m_codec_ctx_init().
 */
void v4l2_m2m_codec_ctx_cleanup(struct v4l2_m2m_codec_ctx *ctx)
{
	v4l2_fh_del(&ctx->fh);
	v4l2_fh_exit(&ctx->fh);
	v4l2_m2m_ctx_release(ctx->fh.m2m_ctx);
	v4l2_m2m_codec_cleanup_ctrls(ctx);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_ctx_cleanup);

/**
 * v4l2_m2m_codec_run_preamble() - Preamble to a codec run
 * @ctx: the context the run is happening on
 * @run: object describing a codec run
 *
 * Prepare a codec run. The src/dst buffer are retrieved and stored in
 * the run object, and v4l2_ctrl_request_setup() is called on the media
 * request attached to the src buffer if there's one.
 * We also copy the src buffer metadata to the dst buffer.
 */
void v4l2_m2m_codec_run_preamble(struct v4l2_m2m_codec_ctx *ctx,
				 struct v4l2_m2m_codec_run *run)
{
	struct media_request *src_req;

	memset(run, 0, sizeof(*run));

	run->bufs.src = v4l2_m2m_next_src_buf(ctx->fh.m2m_ctx);
	run->bufs.dst = v4l2_m2m_next_dst_buf(ctx->fh.m2m_ctx);

	/* Apply request(s) controls if needed. */
	src_req = run->bufs.src->vb2_buf.req_obj.req;
	if (src_req)
		v4l2_ctrl_request_setup(src_req, &ctx->ctrl_hdl);

	v4l2_m2m_buf_copy_metadata(run->bufs.src, run->bufs.dst, true);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_run_preamble);

/**
 * v4l2_m2m_codec_run_postamble() - Postamble to a codec run
 * @ctx: the context the run is happening on
 * @run: the codec run object
 *
 * Finish a run by declaring the request attached to the src buffer as
 * complete.
 */
void v4l2_m2m_codec_run_postamble(struct v4l2_m2m_codec_ctx *ctx,
				  struct v4l2_m2m_codec_run *run)
{
	struct media_request *src_req = run->bufs.src->vb2_buf.req_obj.req;

	if (src_req)
		v4l2_ctrl_request_complete(src_req, &ctx->ctrl_hdl);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_run_postamble);

/**
 * v4l2_m2m_codec_job_finish() - Declare the current job as finished
 * @ctx: context this job is running on
 * @state: state of the job
 *
 * Should be called when the codec is done encoding/decoding a frame.
 * The src/dst buffers are returned to their queues and
 * v4l2_m2m_job_finish() is called.
 */
void v4l2_m2m_codec_job_finish(struct v4l2_m2m_codec_ctx *ctx,
			       enum vb2_buffer_state state)
{
	struct vb2_v4l2_buffer *src_buf, *dst_buf;

	src_buf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
	WARN_ON(!src_buf);
	if (src_buf)
		v4l2_m2m_buf_done(src_buf, state);

	dst_buf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);
	WARN_ON(!dst_buf);
	if (dst_buf)
		v4l2_m2m_buf_done(dst_buf, state);

	v4l2_m2m_job_finish(ctx->codec->m2m_dev, ctx->fh.m2m_ctx);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_job_finish);

/**
 * v4l2_m2m_codec_request_validate() - Validate a media request
 * @req: the media request to validate
 *
 * This function makes sure there's at least one src buffer attached to the
 * request and checks that all per-request+mandatory controls have been set.
 * This helper can be used to implement media_device_ops->req_validate() op.
 */
int v4l2_m2m_codec_request_validate(struct media_request *req)
{
	const struct v4l2_m2m_codec_ctrls *ctrls;
	struct v4l2_m2m_codec_ctx *ctx;
	struct v4l2_ctrl_handler *hdl;
	struct vb2_buffer *vb;
	unsigned int count;
	unsigned int i;

	vb = vb2_request_get_buf(req, 0);
	if (!vb)
		return -ENOENT;

	ctx = vb2_get_drv_priv(vb->vb2_queue);
	if (!ctx)
		return -EINVAL;

	count = vb2_request_buffer_cnt(req);
	if (!count)
		return -ENOENT;
	else if (count > 1)
		return -EINVAL;

	hdl = v4l2_ctrl_request_hdl_find(req, &ctx->ctrl_hdl);
	if (!hdl)
		return -ENOENT;

	ctrls = ctx->coded_fmt_desc->ctrls;
	for (i = 0; ctrls && i < ctrls->num_ctrls; i++) {
		u32 id = ctrls->ctrls[i].cfg.id;
		struct v4l2_ctrl *ctrl;

		if (!ctrls->ctrls[i].per_request || !ctrls->ctrls[i].mandatory)
			continue;

		ctrl = v4l2_ctrl_request_hdl_ctrl_find(hdl, id);
		if (!ctrl)
			return -ENOENT;
	}

	v4l2_ctrl_request_hdl_put(hdl);

	return vb2_request_validate(req);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_request_validate);

/**
 * v4l2_m2m_codec_find_coded_fmt_desc() - Search for a supported coded format
 *					  based on its 4CC
 * @codec: codec to search the coded format on
 * @fourcc: the 4CC representing this format
 *
 * Return: A coded format desc if a matching format was found, NULL otherwise.
 */
const struct v4l2_m2m_codec_coded_fmt_desc *
v4l2_m2m_codec_find_coded_fmt_desc(struct v4l2_m2m_codec *codec, u32 fourcc)
{
	unsigned int i;

	for (i = 0; i < codec->caps->num_coded_fmts; i++) {
		if (codec->caps->coded_fmts[i].fourcc == fourcc)
			return &codec->caps->coded_fmts[i];
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_find_coded_fmt_desc);

/**
 * v4l2_m2m_codec_enum_framesizes() - Enumerate frame sizes helper
 * @file: opened file descriptor
 * @priv: private data
 * @fsize: framesize object
 *
 * This helper can be used to implement ioctl_ops->vidioc_enum_framesizes().
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int v4l2_m2m_codec_enum_framesizes(struct file *file, void *priv,
				   struct v4l2_frmsizeenum *fsize)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_m2m_codec *codec = vdev_to_v4l2_m2m_codec(vdev);
	const struct v4l2_m2m_codec_coded_fmt_desc *fmt;

	if (fsize->index != 0)
		return -EINVAL;

	fmt = v4l2_m2m_codec_find_coded_fmt_desc(codec, fsize->pixel_format);
	if (!fmt)
		return -EINVAL;

	if (!fmt->frmsize)
		return -EINVAL;

	fsize->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	fsize->stepwise = *fmt->frmsize;
	return 0;

}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_enum_framesizes);

static int v4l2_m2m_codec_enum_coded_fmt(struct file *file, void *priv,
					 struct v4l2_fmtdesc *f)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_m2m_codec *codec = vdev_to_v4l2_m2m_codec(vdev);

	if (f->index >= codec->caps->num_coded_fmts)
		return -EINVAL;

	f->pixelformat = codec->caps->coded_fmts[f->index].fourcc;
	return 0;
}

static int v4l2_m2m_codec_enum_decoded_fmt(struct file *file, void *priv,
					   struct v4l2_fmtdesc *f)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_m2m_codec *codec = vdev_to_v4l2_m2m_codec(vdev);

	if (f->index >= codec->caps->num_decoded_fmts)
		return -EINVAL;

	f->pixelformat = codec->caps->decoded_fmts[f->index].fourcc;
	return 0;
}

/**
 * v4l2_m2m_codec_enum_output_fmt() - Enumerate output formats helper
 * @file: opened file descriptor
 * @priv: private data
 * @f: format descriptor
 *
 * This helper can be used to implement ioctl_ops->vidioc_enum_fmt_vid_out().
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int v4l2_m2m_codec_enum_output_fmt(struct file *file, void *priv,
				   struct v4l2_fmtdesc *f)
{
	struct v4l2_m2m_codec_ctx *ctx = fh_to_v4l2_m2m_codec_ctx(priv);

	if (ctx->codec->type == V4L2_M2M_DECODER)
		return v4l2_m2m_codec_enum_coded_fmt(file, priv, f);

	return v4l2_m2m_codec_enum_decoded_fmt(file, priv, f);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_enum_output_fmt);

/**
 * v4l2_m2m_codec_enum_output_fmt() - Enumerate capture formats helper
 * @file: opened file descriptor
 * @priv: private data
 * @f: format descriptor
 *
 * This helper can be used to implement ioctl_ops->vidioc_enum_fmt_vid_cap().
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int v4l2_m2m_codec_enum_capture_fmt(struct file *file, void *priv,
				    struct v4l2_fmtdesc *f)
{
	struct v4l2_m2m_codec_ctx *ctx = fh_to_v4l2_m2m_codec_ctx(priv);

	if (ctx->codec->type == V4L2_M2M_DECODER)
		return v4l2_m2m_codec_enum_decoded_fmt(file, priv, f);

	return v4l2_m2m_codec_enum_coded_fmt(file, priv, f);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_enum_capture_fmt);

/**
 * v4l2_m2m_codec_g_output_fmt() - Get output format helper
 * @file: opened file descriptor
 * @priv: private data
 * @f: format object
 *
 * This helper can be used to implement
 * ioctl_ops->vidioc_g_fmt_vid_out[_mplane]().
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int v4l2_m2m_codec_g_output_fmt(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct v4l2_m2m_codec_ctx *ctx = fh_to_v4l2_m2m_codec_ctx(priv);

	if (ctx->codec->type == V4L2_M2M_DECODER)
		*f = ctx->coded_fmt;
	else
		*f = ctx->decoded_fmt;

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_g_output_fmt);

/**
 * v4l2_m2m_codec_g_capture_fmt() - Get capture format helper
 * @file: opened file descriptor
 * @priv: private data
 * @f: format object
 *
 * This helper can be used to implement
 * ioctl_ops->vidioc_g_fmt_vid_cap[_mplane]().
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int v4l2_m2m_codec_g_capture_fmt(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct v4l2_m2m_codec_ctx *ctx = fh_to_v4l2_m2m_codec_ctx(priv);

	if (ctx->codec->type == V4L2_M2M_DECODER)
		*f = ctx->decoded_fmt;
	else
		*f = ctx->coded_fmt;

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_g_capture_fmt);

static void
v4l2_m2m_codec_apply_frmsize_constraints(struct v4l2_format *f,
				const struct v4l2_frmsize_stepwise *frmsize)
{
	u32 *width, *height;

	if (!frmsize)
		return;

	if (!V4L2_TYPE_IS_MULTIPLANAR(f->type)) {
		width = &f->fmt.pix.width;
		height = &f->fmt.pix.height;
	} else {
		width = &f->fmt.pix_mp.width;
		height = &f->fmt.pix_mp.height;
	}

	v4l2_apply_frmsize_constraints(width, height, frmsize);
}

static int v4l2_m2m_codec_try_coded_fmt(struct file *file, void *priv,
					struct v4l2_format *f)
{
	struct v4l2_m2m_codec_ctx *ctx = fh_to_v4l2_m2m_codec_ctx(priv);
	const struct v4l2_m2m_codec_coded_fmt_desc *desc;
	struct v4l2_m2m_codec *codec = ctx->codec;
	u32 fourcc;
	int ret;

	if (!V4L2_TYPE_IS_MULTIPLANAR(f->type))
		fourcc = f->fmt.pix.pixelformat;
	else
		fourcc = f->fmt.pix_mp.pixelformat;

	desc = v4l2_m2m_codec_find_coded_fmt_desc(codec, fourcc);
	if (!desc)
		return -EINVAL;

	v4l2_m2m_codec_apply_frmsize_constraints(f, desc->frmsize);

	if (!V4L2_TYPE_IS_MULTIPLANAR(f->type)) {
		f->fmt.pix.field = V4L2_FIELD_NONE;
	} else {
		f->fmt.pix_mp.field = V4L2_FIELD_NONE;
		/* All coded formats are considered single planar for now. */
		f->fmt.pix_mp.num_planes = 1;
	}

	if (desc->ops->adjust_fmt) {
		ret = desc->ops->adjust_fmt(ctx, f);
		if (ret)
			return ret;
	}

	return 0;
}

static int v4l2_m2m_codec_try_decoded_fmt(struct file *file, void *priv,
					  struct v4l2_format *f)
{
	struct v4l2_m2m_codec_ctx *ctx = fh_to_v4l2_m2m_codec_ctx(priv);
	const struct v4l2_m2m_codec_coded_fmt_desc *coded_desc;
	struct v4l2_m2m_codec *codec = ctx->codec;
	unsigned int i;
	u32 fourcc;

	/*
	 * The codec context should point to a coded format desc, if the format
	 * on the coded end has not been set yet, it should point to the
	 * default value.
	 */
	coded_desc = ctx->coded_fmt_desc;
	if (WARN_ON(!coded_desc))
		return -EINVAL;

	if (!V4L2_TYPE_IS_MULTIPLANAR(f->type))
		fourcc = f->fmt.pix.pixelformat;
	else
		fourcc = f->fmt.pix_mp.pixelformat;

	for (i = 0; i < codec->caps->num_decoded_fmts; i++) {
		if (codec->caps->decoded_fmts[i].fourcc == fourcc)
			break;
	}

	if (i == codec->caps->num_decoded_fmts)
		return -EINVAL;

	/* Always apply the frmsize constraint of the coded end. */
	v4l2_m2m_codec_apply_frmsize_constraints(f, coded_desc->frmsize);

	if (!V4L2_TYPE_IS_MULTIPLANAR(f->type)) {
		v4l2_fill_pixfmt(&f->fmt.pix, fourcc, f->fmt.pix.width,
				 f->fmt.pix.height);
		f->fmt.pix.field = V4L2_FIELD_NONE;
	} else {
		v4l2_fill_pixfmt_mp(&f->fmt.pix_mp, fourcc, f->fmt.pix_mp.width,
				    f->fmt.pix_mp.height);
		f->fmt.pix_mp.field = V4L2_FIELD_NONE;
	}

	return 0;
}

/**
 * v4l2_m2m_codec_try_output_fmt() - Try output format helper
 * @file: opened file descriptor
 * @priv: private data
 * @f: format object
 *
 * This helper can be used to implement
 * ioctl_ops->vidioc_try_fmt_vid_out[_mplane]().
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int v4l2_m2m_codec_try_output_fmt(struct file *file, void *priv,
				  struct v4l2_format *f)
{
	struct v4l2_m2m_codec_ctx *ctx = fh_to_v4l2_m2m_codec_ctx(priv);

	if (ctx->codec->type == V4L2_M2M_DECODER)
		return v4l2_m2m_codec_try_coded_fmt(file, priv, f);

	return v4l2_m2m_codec_try_decoded_fmt(file, priv, f);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_try_output_fmt);

/**
 * v4l2_m2m_codec_try_capture_fmt() - Try capture format helper
 * @file: opened file descriptor
 * @priv: private data
 * @f: format object
 *
 * This helper can be used to implement
 * ioctl_ops->vidioc_try_fmt_vid_cap[_mplane]().
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int v4l2_m2m_codec_try_capture_fmt(struct file *file, void *priv,
				   struct v4l2_format *f)
{
	struct v4l2_m2m_codec_ctx *ctx = fh_to_v4l2_m2m_codec_ctx(priv);

	if (ctx->codec->type == V4L2_M2M_DECODER)
		return v4l2_m2m_codec_try_decoded_fmt(file, priv, f);

	return v4l2_m2m_codec_try_coded_fmt(file, priv, f);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_try_capture_fmt);

static int v4l2_m2m_codec_s_fmt(struct file *file, void *priv,
				struct v4l2_format *f,
				int (*try_fmt)(struct file *, void *,
					       struct v4l2_format *))
{
	struct v4l2_m2m_codec_ctx *ctx = fh_to_v4l2_m2m_codec_ctx(priv);
	struct vb2_queue *vq;
	int ret;

	if (!try_fmt)
		return -EINVAL;

	vq = v4l2_m2m_get_vq(ctx->fh.m2m_ctx, f->type);
	if (vb2_is_busy(vq))
		return -EBUSY;

	ret = try_fmt(file, priv, f);
	if (ret)
		return ret;

	if (V4L2_TYPE_IS_OUTPUT(f->type) ==
	    (ctx->codec->type == V4L2_M2M_DECODER)) {
		struct v4l2_m2m_ctx *m2m_ctx = v4l2_m2m_codec_get_m2m_ctx(ctx);
		const struct v4l2_m2m_codec_coded_fmt_desc *desc;
		u32 fourcc;

		if (!V4L2_TYPE_IS_MULTIPLANAR(f->type))
			fourcc = f->fmt.pix.pixelformat;
		else
			fourcc = f->fmt.pix_mp.pixelformat;

		desc = v4l2_m2m_codec_find_coded_fmt_desc(ctx->codec, fourcc);
		if (!desc)
			return -EINVAL;

		ctx->coded_fmt_desc = desc;
		m2m_ctx->out_q_ctx.q.requires_requests = desc->requires_requests;
	}

	return 0;
}

/**
 * v4l2_m2m_codec_s_output_fmt() - Set output format helper
 * @file: opened file descriptor
 * @priv: private data
 * @f: format object
 *
 * This helper can be used to implement
 * ioctl_ops->vidioc_s_fmt_vid_out[_mplane]().
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int v4l2_m2m_codec_s_output_fmt(struct file *file, void *priv,
				struct v4l2_format *f)
{
	struct v4l2_m2m_codec_ctx *ctx = fh_to_v4l2_m2m_codec_ctx(priv);
	struct video_device *vfd = video_devdata(file);
	const struct v4l2_ioctl_ops *ops = vfd->ioctl_ops;
	struct v4l2_format *cap_fmt;
	int ret;

	ret = v4l2_m2m_codec_s_fmt(file, priv, f,
				   V4L2_TYPE_IS_MULTIPLANAR(f->type) ?
				   ops->vidioc_try_fmt_vid_out_mplane :
				   ops->vidioc_try_fmt_vid_out);
	if (ret)
		return ret;

	if (ctx->codec->type == V4L2_M2M_DECODER) {
		ctx->coded_fmt = *f;
		cap_fmt = &ctx->decoded_fmt;
	} else {
		ctx->decoded_fmt = *f;
		cap_fmt = &ctx->coded_fmt;
	}

	/* Propagate colorspace information to capture. */
	if (V4L2_TYPE_IS_MULTIPLANAR(f->type)) {
		cap_fmt->fmt.pix_mp.colorspace = f->fmt.pix_mp.colorspace;
		cap_fmt->fmt.pix_mp.xfer_func = f->fmt.pix_mp.xfer_func;
		cap_fmt->fmt.pix_mp.ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
		cap_fmt->fmt.pix_mp.quantization = f->fmt.pix_mp.quantization;
	} else {
		cap_fmt->fmt.pix.colorspace = f->fmt.pix.colorspace;
		cap_fmt->fmt.pix.xfer_func = f->fmt.pix.xfer_func;
		cap_fmt->fmt.pix.ycbcr_enc = f->fmt.pix.ycbcr_enc;
		cap_fmt->fmt.pix.quantization = f->fmt.pix.quantization;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_s_output_fmt);

/**
 * v4l2_m2m_codec_s_capture_fmt() - Set capture format helper
 * @file: opened file descriptor
 * @priv: private data
 * @f: format object
 *
 * This helper can be used to implement
 * ioctl_ops->vidioc_s_fmt_vid_cap[_mplane]().
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int v4l2_m2m_codec_s_capture_fmt(struct file *file, void *priv,
				 struct v4l2_format *f)
{
	struct v4l2_m2m_codec_ctx *ctx = fh_to_v4l2_m2m_codec_ctx(priv);
	struct video_device *vfd = video_devdata(file);
	const struct v4l2_ioctl_ops *ops = vfd->ioctl_ops;
	int ret;

	ret = v4l2_m2m_codec_s_fmt(file, priv, f,
				   V4L2_TYPE_IS_MULTIPLANAR(f->type) ?
				   ops->vidioc_try_fmt_vid_cap_mplane:
				   ops->vidioc_try_fmt_vid_cap);
	if (ret)
		return ret;

	if (ctx->codec->type == V4L2_M2M_DECODER)
		ctx->decoded_fmt = *f;
	else
		ctx->coded_fmt = *f;

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_s_capture_fmt);

/**
 * v4l2_m2m_codec_queue_setup() - Queue setup helper
 * @vq: the VB2 queue to setup
 * @num_buffers: the number of buffers to allocate
 * @num_planes: the number of planes per image
 * @sizes: the plane sizes
 * @alloc_devs: the device to use to allocate each plane
 *
 * This helper can be used to implement vb2_ops->queue_setup().
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int v4l2_m2m_codec_queue_setup(struct vb2_queue *vq, unsigned int *num_buffers,
			       unsigned int *num_planes, unsigned int sizes[],
			       struct device *alloc_devs[])
{
	struct v4l2_m2m_codec_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_format *f;
	int i;

	if (V4L2_TYPE_IS_OUTPUT(vq->type) ==
	    (ctx->codec->type == V4L2_M2M_DECODER))
		f = &ctx->coded_fmt;
	else
		f = &ctx->decoded_fmt;

	if (!V4L2_TYPE_IS_MULTIPLANAR(vq->type)) {
		if (*num_planes) {
			if (*num_planes != 1)
				return -EINVAL;

			if (sizes[0] < f->fmt.pix.sizeimage)
				return -EINVAL;
		} else {
			sizes[0] = f->fmt.pix.sizeimage;
		}

		return 0;
	}

	if (*num_planes) {
		if (*num_planes != f->fmt.pix_mp.num_planes)
			return -EINVAL;

		for (i = 0; i < f->fmt.pix_mp.num_planes; i++) {
			if (sizes[i] < f->fmt.pix_mp.plane_fmt[i].sizeimage)
				return -EINVAL;
		}

		return 0;
	}

	*num_planes = f->fmt.pix_mp.num_planes;
	for (i = 0; i < f->fmt.pix_mp.num_planes; i++)
		sizes[i] = f->fmt.pix_mp.plane_fmt[i].sizeimage;

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_queue_setup);

/**
 * v4l2_m2m_codec_queue_cleanup() - Queue cleanup helper
 * @vq: the VB2 queue to cleanup
 * @state: the sate to assigned to released bufs
 *
 * This helper can be used in the vb2_ops->stop_streaming() implementation
 * to release buffers (and associated requests) bound to a queue.
 */
void v4l2_m2m_codec_queue_cleanup(struct vb2_queue *vq, u32 state)
{
	struct v4l2_m2m_codec_ctx *ctx = vb2_get_drv_priv(vq);

	while (true) {
		struct vb2_v4l2_buffer *vbuf;

		if (V4L2_TYPE_IS_OUTPUT(vq->type))
			vbuf = v4l2_m2m_src_buf_remove(ctx->fh.m2m_ctx);
		else
			vbuf = v4l2_m2m_dst_buf_remove(ctx->fh.m2m_ctx);

		if (!vbuf)
			break;

		v4l2_ctrl_request_complete(vbuf->vb2_buf.req_obj.req,
					   &ctx->ctrl_hdl);
		v4l2_m2m_buf_done(vbuf, state);
	}
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_queue_cleanup);

/**
 * v4l2_m2m_codec_buf_out_validate() - Validate output buffer helper
 * @vb: the VB2 buffer to validate
 *
 * This helper can be used to implement vb2_ops->buf_out_validate().
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int v4l2_m2m_codec_buf_out_validate(struct vb2_buffer *vb)
{
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	vbuf->field = V4L2_FIELD_NONE;
	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_buf_out_validate);

/**
 * v4l2_m2m_codec_buf_prepare() - Prepare buffer helper
 * @vb: the VB2 buffer to prepare
 *
 * This helper can be used to implement vb2_ops->buf_prepare().
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int v4l2_m2m_codec_buf_prepare(struct vb2_buffer *vb)
{
	struct vb2_queue *vq = vb->vb2_queue;
	struct v4l2_m2m_codec_ctx *ctx = vb2_get_drv_priv(vq);
	struct v4l2_format *f;
	int i;

	if (V4L2_TYPE_IS_OUTPUT(vq->type) ==
	    (ctx->codec->type == V4L2_M2M_DECODER))
		f = &ctx->coded_fmt;
	else
		f = &ctx->decoded_fmt;

	if (!V4L2_TYPE_IS_MULTIPLANAR(vq->type)) {
		if (vb2_plane_size(vb, 0) < f->fmt.pix.sizeimage)
			return -EINVAL;

		return 0;
	}

	for (i = 0; i < f->fmt.pix_mp.num_planes; ++i) {
		if (vb2_plane_size(vb, i) < f->fmt.pix_mp.plane_fmt[i].sizeimage)
			return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_buf_prepare);

/**
 * v4l2_m2m_codec_buf_queue() - Queue buffer helper
 * @vb: the VB2 buffer to queue
 *
 * This helper can be used to implement vb2_ops->buf_queue().
 */
void v4l2_m2m_codec_buf_queue(struct vb2_buffer *vb)
{
	struct v4l2_m2m_codec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);
	struct vb2_v4l2_buffer *vbuf = to_vb2_v4l2_buffer(vb);

	v4l2_m2m_buf_queue(ctx->fh.m2m_ctx, vbuf);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_buf_queue);

/**
 * v4l2_m2m_codec_buf_queue() - Complete buffer request helper
 * @vb: the VB2 buffer to complete request on
 *
 * This helper can be used to implement vb2_ops->buf_request_complete().
 */
void v4l2_m2m_codec_buf_request_complete(struct vb2_buffer *vb)
{
	struct v4l2_m2m_codec_ctx *ctx = vb2_get_drv_priv(vb->vb2_queue);

	v4l2_ctrl_request_complete(vb->req_obj.req, &ctx->ctrl_hdl);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_buf_request_complete);

/**
 * v4l2_m2m_codec_start_streaming() - Start streaming helper
 * @q: the VB2 queue to start streaming on
 *
 * This helper can be used to implement vb2_ops->start_streaming().
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int v4l2_m2m_codec_start_streaming(struct vb2_queue *q, unsigned int count)
{
	struct v4l2_m2m_codec_ctx *ctx = vb2_get_drv_priv(q);
	const struct v4l2_m2m_codec_coded_fmt_desc *desc;
	struct v4l2_m2m_codec *codec = ctx->codec;
	int ret;

	if ((codec->type == V4L2_M2M_DECODER) != V4L2_TYPE_IS_OUTPUT(q->type))
		return 0;

	desc = ctx->coded_fmt_desc;
	if (WARN_ON(!desc))
		return -EINVAL;

	if (desc->ops->start) {
		ret = desc->ops->start(ctx);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_start_streaming);

/**
 * v4l2_m2m_codec_stop_streaming() - Stop streaming helper
 * @q: the VB2 queue to stop streaming on
 *
 * This helper can be used to implement vb2_ops->stop_streaming().
 */
void v4l2_m2m_codec_stop_streaming(struct vb2_queue *q)
{
	struct v4l2_m2m_codec_ctx *ctx = vb2_get_drv_priv(q);
	const struct v4l2_m2m_codec_coded_fmt_desc *desc;
	struct v4l2_m2m_codec *codec = ctx->codec;

	if ((codec->type == V4L2_M2M_DECODER) == V4L2_TYPE_IS_OUTPUT(q->type)) {
		desc = ctx->coded_fmt_desc;
		if (WARN_ON(!desc))
			return;

		if (desc->ops->stop)
			desc->ops->stop(ctx);
	}

	v4l2_m2m_codec_queue_cleanup(q, VB2_BUF_STATE_ERROR);
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_stop_streaming);

/**
 * v4l2_m2m_codec_device_run() - Device run helper
 * @ctx: the codec context this run happens on
 *
 * This helper can be used to implement v4l2_m2m_ops->device_run(). It just
 * calls the ->run() method of the selected coded format.
 *
 * Return: 0 in case of success, a negative error code otherwise.
 */
int v4l2_m2m_codec_device_run(struct v4l2_m2m_codec_ctx *ctx)
{
	const struct v4l2_m2m_codec_coded_fmt_desc *desc;

	desc = ctx->coded_fmt_desc;
	if (WARN_ON(!desc))
		return -EINVAL;

	desc->ops->run(ctx);
	return 0;
}
EXPORT_SYMBOL_GPL(v4l2_m2m_codec_device_run);
