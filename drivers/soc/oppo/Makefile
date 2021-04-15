obj-y	+=  oppo_project/
obj-y	+=  oppo_devinfo/
obj-y   +=  oppo_als_cali_value/
obj-y	+=  oppo_sensorinfo/
obj-y	+=  oppo_gpio/
obj-$(CONFIG_OPPO_HYPNUS)     +=  oppo_hypnus/

#ifdef VENDOR_EDIT
#wen.luo@PSW.Kernel.MM, 2018/05/23, Add for Healthinfo monitor
#Liang.Zhang@TECH.Storage.Stability.OPPO_SHUTDOWN_DETECT, 2019/04/28, Add for shutdown detect
obj-$(CONFIG_OPPO_SHUTDOWN_DETECT)	+=  oppo_shutdown_detect/
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
#//wenbin.liu@PSW.Kernel.MM, 2018/05/23, Add for Healthinfo monitor
obj-y     +=  oppo_healthinfo/
#endif /* VENDOR_EDIT */

#ifdef VENDOR_EDIT
#zhenwenxian@BSP.Power.Basic, 2017/01/12, Add for NV recovery
#obj-$(CONFIG_RECORD_MDMRST)     +=  oppo_mdmrst/
obj-y     +=  oppo_mdmrst/
#endif /* VENDOR_EDIT */

#ifdefine VENDOR_EDIT
#GaoTing.Gan@PSW.MultiMedia.MediaServer, 2019/03/07, Add for kevent feedback
obj-y +=  oppo_kevent_feedback/
#endif /* VENDOR_EDIT */
