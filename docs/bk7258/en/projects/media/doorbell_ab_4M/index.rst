Doorbell_ab_4M
=================================

:link_to_translation:`zh_CN:[中文]`

1. Introduction
---------------------------------

This project is a demo of a USB camera door lock, supporting end-to-end (BK7258 device) to mobile app demonstrations. By default, it supports Shangyun for network transmission.

1.1 Specifications
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    * Hardware configuration:
        * Core board, **BK7258_QFN88_9X9_V3.2**
        * Display adapter board, **BK7258_LCD_interface_V3.0**
        * Speaker small board, **BK_Module_Speaker_V1.1**
        * PSRAM 8M/16M
    * Support, UVC
        * Reference peripherals, UVC resolution of **864 * 480**
    * Support, UAC
    * Support, TCP LAN image transmission
    * Support UDP LAN image transmission
    * Support, Shangyun, P2P image transfer
    * Support, LCD RGB/MCU I8080 display
        * Reference peripherals, **ST7701SN**, 480 * 854 RGB LCD
        * RGB565/RGB888
    * Support, hardware/software rotation
        * 0°, 90°, 180°, 270°
    * Support, onboard speaker
    * Support, MJPEG hardware decoding
        * YUV422
    * Support, MJPEG software decoding
        * YUV420
    * Support, H264 hardware decoding
    * Support, OSD display
        * ARGB888[PNG]
        * Custom Font

1.2 Path
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    <bk_avdk source code path>/projects/media/doorbell_ab_4M

2. Framework diagram
---------------------------------

    Please refer to `Framework diagram <../../media/doorbell/index.html#framework-diagram>`_

3. Configuration
---------------------------------

    Please refer to `Configuration <../../media/doorbell/index.html#configuration>`_

3.1 Differences
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    The difference between doorbell_ab_4M and doorbell_8M is that the former does not support DVP camera, nor does it support onboard MIC. The PSRAM size and model are also inconsistent.

    .. figure:: ../../../../_static/doorbell_ab_4M_8M_different.png
        :align: center
        :alt: doorbell_ab_4M_8M_different
        :figclass: align-center

    Figure 1. The main partition difference diagram of 4M & 8M

    To modify the PSRAM size from 8M to 4M_ab, you need to make changes to the config file. The file path is:

    doorbell_ab_4M/config/bk7258/config

    doorbell_ab_4M/config/bk7258_cp1/config

    doorbell_ab_4M/config/bk7258_cp2/config

    The main differences in configuration parameters between doorbell_8M and doorbell_ab_4M are as follows (key configurations with no differences are also listed):

     +------------------------------------+---------------------------------+------------------------------------+
     | project                            |        doorbell_8M              |          doorbell_ab_4M            |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_PSRAM_MEM_SLAB_USER_SIZE    |            102400               |                0                   |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_PSRAM_MEM_SLAB_AUDIO_SIZE   |            102400               |              51200                 |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_PSRAM_MEM_SLAB_ENCODE_SIZE  |            1433600              |             946176                 |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_PSRAM_MEM_SLAB_DISPLAY_SIZE |            5701632              |             2490368                |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_MEDIA_PSRAM_SIZE_4M         |              N                  |                Y                   |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_BUCK_ANALOG_DISABLE         |              N                  |                Y                   |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_PSRAM_W955D8MKY_5J          |              N                  |                Y                   |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_PSRAM_APS6408L_O            |              N                  |                N                   |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_CPU0_SPE_RAM_SIZE           |           0X56000               |           0X5E000                  |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_CPU1_APP_RAM_SIZE           |           0X3F000               |           0X38000                  |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_CPU2_APP_RAM_SIZE           |           0XB000                |           0XA000                   |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_PSRAM_HEAP_BASE             |          0x60700000             |          0x60354000                |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_PSRAM_HEAP_SIZE             |           0x80000               |          0x7D000                   |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_PSRAM_HEAP_CPU0_BASE_ADDER  |         0x60700000              |          0x60354000                |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_H264_P_FRAME_CNT            |             5                   |               3                    |
     +------------------------------------+---------------------------------+------------------------------------+
     | CONFIG_ALI_MQTT                    |             N                   |               N                    |
     +------------------------------------+---------------------------------+------------------------------------+


    The flow is as shown in the following diagram:

    .. figure:: ../../../../_static/decode_proc_4M.png
        :align: center
        :alt: 4M decode diagram Overview
        :figclass: align-center

    Figure 2. doorbell_ab_4M decode process

    .. figure:: ../../../../_static/decode_proc_8M.png
        :align: center
        :alt: 8M decode diagram Overview
        :figclass: align-center

    Figure 3. doorbell_8M decode process


    The main differences in the process are as shown in the following table:

    +------------------+--------------------------------------------------------------------------------------------------------------------------------+
    | project          |          decode process                                                                                                        |
    +------------------+--------------------------------------------------------------------------------------------------------------------------------+
    | doorbell_ab_4M   |Firstly, attempt to obtain the YUV image, and continue with the decoding only after the allocation is successful.               |
    |                  |                                                                                                                                |
    |                  |The LCD display triggers the next image capture process immediately after completion.                                           |
    +------------------+--------------------------------------------------------------------------------------------------------------------------------+
    | doorbell_8M      |Directly decode, and upon failure to obtain the YUV image, immediately release the JPEG and wait for the next frame of JPEG.    |
    +------------------+--------------------------------------------------------------------------------------------------------------------------------+


4. Demonstration explanation
---------------------------------

    Please visit `APP Usage Document <https://docs.bekencorp.com/arminodoc/bk_app/app/zh_CN/v2.0.1/app_usage/app_usage_guide/index.html#debug>`__

    Demo result: During runtime, UVC, LCD, and AUDIO will be activated. The LCD will display UVC and output JPEG (864X480) images that have been decoded and rotated 90 degrees before being displayed on the LCD (480X854),
    After decoding, the YUV is encoded with H264 and transmitted to the mobile phone for display via WIFI (864X480).

.. hint::
    If you do not have cloud account permissions, you can use debug mode to set the local area network TCP/UDP image transmission method.


5. Code explanation
---------------------------------

    Please refer to `Code explanation <../../media/doorbell/index.html#code-explanation>`_

6. Porting Instructions
---------------------------------

    For the media module, the biggest difference between the 4M and 8M configurations is the reduction in PSRAM size, which in turn reduces the number of internal buffer images, as shown in the following table:

    +------------------+---------------------------------+-------------------------------+-------------------------------+
    | project          |          YUV images             |     JPEG images               |      H264 images              |
    +------------------+---------------------------------+-------------------------------+-------------------------------+
    | doorbell_ab_4M   |             3                   |      4                        |      4                        |
    +------------------+---------------------------------+-------------------------------+-------------------------------+
    | doorbell_8M      |             5                   |      4                        |      8                        |
    +------------------+---------------------------------+-------------------------------+-------------------------------+

    To modify the project from 8M FLASH + 8M PSRAM to 4M FLASH + 4M PSRAM, follow the steps below:


Step 1:
,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,

    Merge the platform code into the project.

    Synchronize modifications according to the patch, with the patch commit title being "adapter for new 4+4 psram of W955D8MKY",

    There are a total of four commits, and the code directory and involved files are as shown in the following table:

    +---------------------------------+-------------------------------------------------------------------------+
    |          Code Directory         |     Involved Files                                                      |
    +---------------------------------+-------------------------------------------------------------------------+
    |middleware                       | driver/pwr_clk/Kconfig                                                  |
    |                                 |                                                                         |
    |                                 | soc/bk7258/hal/sys_pm_hal.c                                             |
    |                                 |                                                                         |
    |                                 | soc/common/hal/include/psram_hal.h                                      |
    |                                 |                                                                         |
    |                                 | soc/common/hal/psram_hal.c                                              |
    +---------------------------------+-------------------------------------------------------------------------+
    |tools/build_tools                |part_table_tools/otherScript/special_project_deal.py                     |
    |                                 |                                                                         |
    +---------------------------------+-------------------------------------------------------------------------+
    |bk_idk/components/part_table     |CMakeLists.txt                                                           |
    |                                 |                                                                         |
    |                                 |part_table.mk                                                            |
    +---------------------------------+-------------------------------------------------------------------------+
    |projects                         |media/doorbell_ab_4M/CMakeLists.txt                                      |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/config/bk7258_cp1/config                            |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/config/bk7258_cp2/config                            |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/config/bk7258/ab_position_independent.csv           |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/config/bk7258/bk7258_partitions.csv                 |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/config/bk7258/config                                |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/config/bk7258/configuration.json                    |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/config/bk7258/configurationab.json                  |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/config/bk7258/partitions.csv                        |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/config/ota_rbl.config                               |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/main/app_main.c                                     |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/main/CMakeLists.txt                                 |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/main/Kconfig.projbuild                              |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/main/vendor_flash.c                                 |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/main/vendor_flash_partition.h                       |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/pj_config.mk                                        |
    |                                 |                                                                         |
    |                                 |media/doorbell_ab_4M/README.md                                           |
    +---------------------------------+-------------------------------------------------------------------------+

    Key modification points are as shown in the following table:

    +-------------------------------------------------------------------+-------------------------------------------------------------------------+
    |     Involved Files                                                |          Key modification points                                        |
    +-------------------------------------------------------------------+-------------------------------------------------------------------------+
    |driver/pwr_clk/Kconfig                                             |Introduce BUCK_ANALOG_DISABLE macro to disable analog domain BUCK        |
    +-------------------------------------------------------------------+-------------------------------------------------------------------------+
    |soc/bk7258/hal/sys_pm_hal.c                                        |Configuring the actual code to disable the mock domain BUCK              |
    +-------------------------------------------------------------------+-------------------------------------------------------------------------+
    |soc/common/hal/include/psram_hal.h                                 |Add new configuration mode and ID information for increasing 4M PSRAM    |
    +-------------------------------------------------------------------+-------------------------------------------------------------------------+
    |soc/common/hal/psram_hal.c                                         |Adding the initialization process for 4M PSRAM                           |
    +-------------------------------------------------------------------+-------------------------------------------------------------------------+
    |part_table_tools/otherScript/special_project_deal.py               |Add compilation processing for the doorbell_ab_4M partition project      |
    +-------------------------------------------------------------------+-------------------------------------------------------------------------+
    |CMakeLists.txt                                                     | add doorbell_ab_4M project                                              |
    |                                                                   |                                                                         |
    |part_table.mk                                                      |                                                                         |
    +-------------------------------------------------------------------+-------------------------------------------------------------------------+
    |media/doorbell_ab_4M/config/bk7258_cp1/config                      |Enable the macro CONFIG_MEDIA_PSRAM_SIZE_4M                              |
    |                                                                   |                                                                         |
    |tmedia/doorbell_ab_4M/config/bk7258_cp2/config                     |                                                                         |
    |                                                                   |                                                                         |
    |media/doorbell_ab_4M/config/bk7258/config                          |                                                                         |
    +-------------------------------------------------------------------+-------------------------------------------------------------------------+
    |media/doorbell_ab_4M/config/bk7258/bk7258_partitions.csv           |Modify the FLASH space allocation to 4M                                  |
    +-------------------------------------------------------------------+-------------------------------------------------------------------------+

