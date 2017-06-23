# video_codec
nvidia and intel video decode, encode


使用Intel/Nvidia显卡加速来处理音视频编解码。
由于工作中多媒体处理比较多， 把自己的这这方面的经验总结，开源到[Github/mojing1999](https://github.com/mojing1999/video_codec)上, 欢迎指教。
整个项目主要包括：
1. 使用Intel Media SDK，封装解码和编码库。
2. 使用Nvidia CUDA TOOLKIT，封装成解码和编码库。
3. 各个编解码库的调用例子。




---
### 1. Intel Media SDK
Intel Media SDK 是 Intel 推出的基于Intel集显的编解码硬件加速技术。详见[Intel Media SDK](). Media SDK 提供了 Decoder，VPP，Encoder，让用户方便的处理视频流。




#### intel_dec project
基于Intel Media SDK的硬解码库。 代码主要包括三个文件：
1. intel_dec.h - 实现 Media SDK 库的封装头文件。
2. intel_dec.cpp - 实现函数。
3. jm_intel_dec.h - 该库导出函数定义头文件。


jm_intel_dec.h 为导出** API **。需要注意的是：
1. 用户每次输入需要解码的数据之前，可以调用API jm_intel_dec_need_more_data() 来判断解码器是否需要更多的数据。 如果需要更多的数据，请调用 API jm_intel_dec_free_buf_len() 返回解码器需要数据的长度，用户不能输入长度超过返回值。

2.  YUV 帧输出有以下两种方式，用户可以选择一种。
	1.  callback 输出。 在调用 jm_intel_dec_init() 后，设置YUV输出Callback函数 jm_intel_dec_set_yuv_callback(). 如果回调函数指针不为空，可以从回调函数里得到YUV帧。
	2.  手动调用函数获取。 如果不设置回调函数， 可以手动调用 API jm_intel_dec_output_frame()来获取 YUV 帧。 

3.  如果需要停止输入解码数据，请调用API jm_intel_dec_set_eof(), 解码器在解码完缓存的数据后推出。
  

```C
/** 
 *  @desc:   create decode handle
 *   
 *  @return: handle for use
 */
JMDLL_FUNC handle_inteldec jm_intel_dec_create_handle();	//

/** 
 *   @desc:   Init decode before use
 *   @param: codec_type:  0 - H.264,  1 - H.265
 *   @param: out_fmt: output YUV frame format, 0 - NV12, 1 - YV12
 *   @param: handle: decode handle return by jm_intel_dec_create_handle()
 *
 *   @return: 0 - successful, else failed
 */
JMDLL_FUNC int jm_intel_dec_init(int codec_type, int out_fmt, handle_inteldec handle);	//

/** 
 *   @desc:  destroy decode handle
 *   @param: handle: decode handle return by jm_intel_dec_create_handle()
 *
 *   @return: 0 - successful, else failed
 */
JMDLL_FUNC int jm_intel_dec_deinit(handle_inteldec handle);	//

/**
 *   @desc:  set yuv output callback, if callback non null, yuv will output to callback, API jm_intel_dec_output_frame will  no yuv output.
 *	 @param: user_data: callback param.
 *	 @param: callback: callback function.
 *   @param: handle: decode handle return by jm_intel_dec_create_handle()
 *
 *   @return: 0 - successful, else failed
 */
JMDLL_FUNC int jm_intel_dec_set_yuv_callback(void *user_data, HANDLE_YUV_CALLBACK callback, handle_inteldec handle);

/** 
 *   @desc:   decode video frame
 *   @param: in_buf[in]: video frame data, 
 *   @param: in_data_len[in]: data length
 *   @param: handle: decode handle fater init by jm_intel_dec_init()
 *
 *   @return: 0 - successful, else failed
 */
JMDLL_FUNC int jm_intel_dec_input_data(unsigned char *in_buf, int in_data_len, handle_inteldec handle);

/** 
 *   @desc:  get yuv frame, if no data output, will return failed. If user has been set YUV callback function, this API wil no yuv output.
 *   @param: out_buf[out]: output YUV data buffer
 *   @param: out_len[out]: we cab set out_buf = NULL, output yuv frame size.
 *   @param: handle: decode handle fater init by jm_intel_dec_init()
 *
 *   @return: 0 - successful, else failed
 */
JMDLL_FUNC int jm_intel_dec_output_frame(unsigned char *out_buf, int *out_len, handle_inteldec handle);


/*
 *	@desc:	no more data input, set eof to decode, output decoder cached frame
 */
JMDLL_FUNC int jm_intel_dec_set_eof(int is_eof, handle_inteldec handle);


/** 
 *   @desc:  show decode informationt
 *   @param: handle: decode handle fater init by jm_intel_dec_init()
 *
 *   @return: return char * 
 */
JMDLL_FUNC char *jm_intel_dec_stream_info(handle_inteldec handle);

/**
 *   @desc:  check whether decode need more input data.
 *   @param: handle: decode handle fater init by jm_intel_dec_init()
 *
 *   @return: if need more input data, return true, else return false.
 */
JMDLL_FUNC bool jm_intel_dec_need_more_data(handle_inteldec handle);

/**
 *   @desc:  get decode input data buffer free length, app can not input data greater than return length
 *   @param: handle: decode handle fater init by jm_intel_dec_init()
 *
 *   @return: return free buffer length
 */
JMDLL_FUNC int jm_intel_dec_free_buf_len(handle_inteldec handle);

/**
 *   @desc:  after app set eof to decode, decode will output the cached frame, then exit
 *   @param: handle: decode handle fater init by jm_intel_dec_init()
 *
 *   @return: return true if decode exit, else return false.
 */
JMDLL_FUNC bool jm_intel_dec_is_exit(handle_inteldec handle);


```


#### 调用流程
```
handle_yuv_frame_callback()

...

jm_intel_dec_create_handle()
jm_intel_dec_init()
jm_intel_dec_set_yuv_callback() [OPTIONAL]

loop jm_intel_dec_is_exit()
	if(jm_intel_dec_need_more_data()) {
		len = jm_intel_dec_free_buf_len()
		jm_intel_dec_input_data()

		...
		
		//if no more input data
		jm_intel_dec_set_eof()
	}
	
	jm_intel_dec_output_frame() [OPTIONAL]
	//handle yuv frame 

end loop

jm_intel_dec_stream_info()

jm_intel_dec_deinit()

```

#### 测试程序
OS: Win10
CPU: i7-6700
Memory: 16 GB

- 测试解码H.264 文件1， 分辨率为 4096 x 2048 ，输出为YUV420 NV12，不写硬盘， 结果见如下：

```
==========================================
Codec:          H.264
Display:        4096 x 2048
Pixel Format:   NV12
Frame Count:    283
Elapsed Time:   2169 ms
Decode FPS:     130.474873 fps
==========================================
```

- 测试文件2， 分辨率为 2704 x 1520, 不写硬盘，结果如下：

```
==========================================
Codec:          H.264
Display:        2704 x 1520
Pixel Format:   NV12
Frame Count:    1363
Elapsed Time:   5388 ms
Decode FPS:     252.969562 fps
==========================================
```

更多测试，可以同时解码多路视频。