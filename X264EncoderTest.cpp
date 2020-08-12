#include <cstdint>
#include <string>
#include <sstream>
#include <fstream>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "turbojpeg.h"
#include "X264EncoderTest.h"
const char* v1::x264::Encoder::images2h264(v1::x264::JpegDataStruct* jpgBuffers, int frameTotal, long* x264Size)
{
    int i_frame = 0;
    // 帧大小
    int i_frame_size = 0;

    /* Encode frames */
    std::ostringstream oss;
    for (; i_frame < frameTotal; i_frame++)
    {
        v1::x264::JpegDataStruct jpegInfo = jpgBuffers[i_frame];
        if (jpeg2yuv(this->yuv_buffer, jpegInfo.jpegBytes, jpegInfo.jpegSize, height, width) == -1) {
            printf("jpeg转yuv失败\n");
            return nullptr;
        }

        int off = 0;
        memcpy(this->luma_data, this->yuv_buffer + off, this->luma_size);
        this->pic.img.plane[0] = this->luma_data;
        off = this->luma_size;
        memcpy(this->chroma_data_1, this->yuv_buffer + off, this->chroma_size);
        this->pic.img.plane[1] = this->chroma_data_1;

        off = this->luma_size + this->chroma_size;
        memcpy(this->chroma_data_2, this->yuv_buffer + off, this->chroma_size);
        this->pic.img.plane[2] = this->chroma_data_2;

        this->pic.i_pts = i_frame;
        i_frame_size = x264_encoder_encode(this->h, &this->nal, &this->i_nal, &this->pic, &this->pic_out);
        if (i_frame_size < 0) {
            return nullptr;
        }
        else if (i_frame_size)
        {
            oss.write((char*)this->nal->p_payload, i_frame_size);
        }
    }
    /* Flush delayed frames */
    while (x264_encoder_delayed_frames(this->h))
    {
        //i_frame_size = x264_encoder_encode(h, &nal, &i_nal, NULL, &pic_out);
        i_frame_size = x264_encoder_encode(this->h, &this->nal, &this->i_nal, NULL, &this->pic_out);
        if (i_frame_size < 0) {
            return nullptr;
        }
        else if (i_frame_size)
        {
            oss.write((char*)this->nal->p_payload, i_frame_size);
        }
    }
    std::string const& str = oss.str();
    char const* intString = str.c_str();
    char* h264data = new char[str.length()];
    memcpy(h264data, intString, str.length());
    *x264Size = str.length();

    // std::ofstream osf("1oxs.h264", std::ios::binary);
    // osf.write(h264data, str.length());
    // osf.close();

    return h264data;
}

bool v1::x264::Encoder::init(int width, int height, int fps, int bitrate)
{
    this->width = width;
    this->height = height;

    /* Get default params for preset/tuning 获取预设/调整的默认参数 */
    if (x264_param_default_preset(&this->param, "fast", NULL) < 0) {
        printf("获取预设/调整的默认参数\n");
        return false;
    } 

    /* Configure non-default params 配置非默认参数 */
    this->param.i_bitdepth = 8;
    this->param.i_csp = X264_CSP_I420;
    this->param.i_width = this->width;
    this->param.i_height = this->height;
    this->param.b_vfr_input = 0;
    this->param.b_repeat_headers = 1;
    this->param.b_annexb = 1;
    // 帧数控制
    this->param.i_fps_den = 1;
    this->param.i_fps_num = fps;

    // 设定码率 ABR(平均码率)
    this->param.rc.i_rc_method = X264_RC_ABR;
    // 平均码率
    this->param.rc.i_bitrate = bitrate;
    // 最大码率
    // this->param.rc.i_vbv_max_bitrate = bitrate * 1.2;

    // 关键帧间隔
    // this->param.i_keyint_min = fps;
    // this->param.i_keyint_max = fps;
    // 是否使用周期帧内刷新替代IDR帧
    this->param.b_intra_refresh = 0;

    this->param.i_log_level = X264_LOG_NONE;
    // 即时编码
    this->param.i_threads = X264_SYNC_LOOKAHEAD_AUTO;
    this->param.rc.i_lookahead = 0;

    // 关闭b帧
    this->param.i_bframe = 0;
    // this->param.rc.b_mb_tree = 0;

    /* Apply profile restrictions. 应用配置文件限制 */
    if (x264_param_apply_profile(&this->param, "high") < 0) {
        printf("x264_param_apply_profile\n");
        return false;

    }
        
    if (x264_picture_alloc(&this->pic, this->param.i_csp, this->param.i_width, this->param.i_height) < 0) {
        printf("x264_picture_alloc\n");
        return false;
    }

    this->h = x264_encoder_open(&this->param);
    if (!this->h) {
        printf("x264_encoder_open\n");
        x264_picture_clean(&this->pic);
        return false;
    }

    this->luma_size = width * height + 1;
    this->chroma_size = luma_size / 4 + 1;

    this->yuv_size = tjBufSizeYUV2(width, 1, height, 2);
    this->yuv_buffer = new v1::x264::byte[yuv_size];
    this->luma_data = new v1::x264::byte[luma_size];
    this->chroma_data_1 = new v1::x264::byte[chroma_size];
    this->chroma_data_2 = new v1::x264::byte[chroma_size];

    return true;
}


int v1::x264::Encoder::jpeg2yuv(unsigned char* yuv_buffer, unsigned char* jpg_buffer, unsigned long jpg_size, int height, int width) {
    tjhandle handle = nullptr;
    int twidth, theight, subsample, colorspace;
    int flags = 0;
    int pad = 1; // 1或4均可，但不能是0
    int ret = 0;

    handle = tjInitDecompress();
    if (nullptr == handle) {
        return -1;
    }

    tjDecompressHeader3(handle, jpg_buffer, jpg_size, &twidth, &theight, &subsample, &colorspace);
    ret = tjDecompressToYUV2(handle, jpg_buffer, jpg_size, yuv_buffer, width, pad, height, flags);
    tjDestroy(handle);
    return ret;
}

void v1::x264::Encoder::release()
{
    if(this->h != NULL) {
        x264_encoder_close(this->h);
    }
    if(&this->pic != NULL){
        pic.img.plane[0] = NULL;
        pic.img.plane[1] = NULL;
        pic.img.plane[2] = NULL;
        x264_picture_clean(&this->pic);
    }
    delete[] yuv_buffer;
    delete[] luma_data;
    delete[] chroma_data_1;
    delete[] chroma_data_2;
}
