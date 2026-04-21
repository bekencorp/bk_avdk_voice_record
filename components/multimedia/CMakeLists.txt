if (CONFIG_MEDIA)
set(incs include pipeline camera)

if (CONFIG_MEDIA_APP)
	set(srcs
		cli/media_cli.c
		cli/camera_switch_test.c
		app/media_app.c
		app/camera_handle_list.c
		comm/comm_act.c
	)
endif()  #CONFIG_MEDIA_APP

if (CONFIG_MEDIA_APP)
	list(APPEND srcs
		app/aud_intf.c
		app/aud_tras.c
	)
endif()

if (CONFIG_MEDIA_APP AND CONFIG_AUD_INTF_TEST)
	list(APPEND srcs
		cli/aud_intf_demo.c
	)
endif()

if (CONFIG_MEDIA_MAJOR)
	set(srcs
		core/media_ui.c
		comm/frame_buffer.c
		camera/camera_act.c
		camera/net_camera.c
	)
endif() #CONFIG_MEDIA_MAJOR

if (CONFIG_MEDIA_MINOR) #CONFIG_MEDIA_MINOR
	set(srcs
		)
endif() #CONFIG_MEDIA_MINOR


if ((CONFIG_CPU_CNT GREATER 1))
	list(APPEND srcs
		mb/mailbox_act.c
	)
endif()


if (CONFIG_DVP_CAMERA)
	list(APPEND srcs
		camera/dvp.c
	)
endif()

if (CONFIG_USB_UVC)
		list(APPEND srcs
		camera/uvc.c
	)
endif()

if (CONFIG_WIFI_TRANSFER)

if (CONFIG_MEDIA_APP)
	list(APPEND srcs
		transfer/transfer_app_act.c
	)
endif()

if (CONFIG_MEDIA_MAJOR)
	list(APPEND srcs
		transfer/transfer_major_act.c
	)
endif()

endif()#CONFIG_WIFI_TRANSFER

if (CONFIG_USB_TRANSFER)

if (CONFIG_MEDIA_APP)
	list(APPEND srcs
		transfer/usb_app_act.c
	)
endif()

if (CONFIG_MEDIA_MAJOR)
	list(APPEND srcs
		transfer/usb_major_act.c
	)
endif()

endif()#CONFIG_USB_TRANSFER

if (CONFIG_IMAGE_STORAGE)

if (CONFIG_MEDIA_APP)
	list(APPEND srcs
		storage/storage_comm.c
		storage/storage_app_act.c
	)
endif()

if (CONFIG_MEDIA_MAJOR)
	list(APPEND srcs
		storage/storage_major_act.c
	)
endif()

endif()#CONFIG_IMAGE_STORAGE

if (CONFIG_LCD)
	list(APPEND srcs
		image_service/img_service.c
	)
endif()


if (CONFIG_LCD_ROTATE OR CONFIG_LCD_HW_PFC)
	list(APPEND srcs
		image_service/bk_rotate.c
		#image_service/lcd_calc.c
	)
endif()

if (CONFIG_LCD_HW_DECODE OR CONFIG_LCD_SW_DECODE)
	list(APPEND srcs
		image_service/bk_decode.c
	)
endif()

if (CONFIG_MEDIA_SCALE)
	list(APPEND srcs
		image_service/bk_scale.c
	)
endif()

if (CONFIG_MEDIA_PIPELINE)
	list(APPEND srcs
		pipeline/jpeg_decode_pipeline.c
		pipeline/lcd_scale_pipeline.c
		pipeline/yuv_rotate_pipeline.c
		pipeline/uvc_pipeline_act.c
		pipeline/jpeg_decode_list.c
		pipeline/jpeg_get_pipeline.c
	)

if (CONFIG_H264)
	list(APPEND srcs
		pipeline/h264_encode_pipeline.c
	)
endif() #CONFIG_H264
endif()

if (CONFIG_MEDIA_MAJOR)
	list(APPEND srcs
		aud/aud_tras_drv.c
		aud/aud_aec.c
	)
endif()

if (CONFIG_MEDIA_MAJOR)
if (CONFIG_MEDIA_BT_AUDIO)
	list(APPEND srcs
		bt/bt_audio_act.c
	)
endif()
endif()



if ((CONFIG_CPU_CNT GREATER 1))
	list(APPEND incs mailbox)
	list(APPEND srcs
		mailbox/media_mailbox_list_util.c
	)

if (CONFIG_MEDIA_APP)
	list(APPEND srcs
		mailbox/media_app_mailbox.c
	)
endif()#CONFIG_MEDIA_APP

if (CONFIG_MEDIA_MAJOR)
	list(APPEND srcs
		mailbox/media_major_mailbox.c
  		rotate/sw_rotate.c
	)
	if (CONFIG_JPEG_SW_DECODE_SUPPORT_BY_FRAME)
		list(APPEND srcs
			decode/sw_decode.c
		)
	endif()#CONFIG_JPEG_SW_DECODE_SUPPORT_BY_FRAME
endif()#CONFIG_MEDIA_MAJOR

if (CONFIG_MEDIA_MINOR)
	list(APPEND srcs
		mailbox/media_minor_mailbox.c
   		rotate/sw_rotate_cp2.c
	)
	if (CONFIG_JPEG_SW_DECODE_SUPPORT_BY_FRAME)
		list(APPEND srcs
			decode/sw_decode_cp2.c
		)
	endif()#CONFIG_JPEG_SW_DECODE_SUPPORT_BY_FRAME
endif()#CONFIG_MEDIA_MINOR

if (CONFIG_AUD_ASR)
	list(APPEND srcs
		mailbox/media_mailbox_asr_tras.c
	)

endif()

endif()#(CONFIG_CPU_CNT GREATER 1)

if (CONFIG_MEDIA_MAJOR)
if (CONFIG_AUD_INTF_SUPPORT_PROMPT_TONE)
	list(APPEND incs prompt_tone_play/include)

	list(APPEND srcs
		prompt_tone_play/src/audio_codec.c
		prompt_tone_play/src/audio_source.c
		prompt_tone_play/src/prompt_tone_play.c
	)

if (CONFIG_PROMPT_TONE_CODEC_MP3)
	list(APPEND srcs
		prompt_tone_play/src/mp3_codec.c
	)
endif()

if (CONFIG_PROMPT_TONE_CODEC_PCM)
	list(APPEND srcs
		prompt_tone_play/src/pcm_codec.c
	)
endif()

if (CONFIG_PROMPT_TONE_CODEC_WAV)
	list(APPEND srcs
		prompt_tone_play/src/wav_codec.c
	)
endif()

if (CONFIG_PROMPT_TONE_SOURCE_VFS)
	list(APPEND srcs
		prompt_tone_play/src/source_vfs.c
	)
endif()

if (CONFIG_PROMPT_TONE_SOURCE_ARRAY)
	list(APPEND srcs
		prompt_tone_play/src/source_array.c
	)
endif()
endif()
endif()

list(APPEND srcs core/media_mem_share.c)

endif()# CONFIG_MEDIA

armino_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incs}" PRIV_REQUIRES bk_cli lvgl avdk_libs unity media_utils bk_draw_blend display_service bk_dvp bk_uvc avdk_utils bk_vfs)
