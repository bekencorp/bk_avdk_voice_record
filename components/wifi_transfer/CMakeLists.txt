set(incs include)
set(srcs)

set(priv_req bk_common multimedia)

if (CONFIG_WIFI_TRANSFER AND CONFIG_SYS_CPU0)
	list(APPEND srcs
		src/wifi_transfer.c
		src/trans_list.c
		)
endif()

armino_component_register(SRCS "${srcs}"
	INCLUDE_DIRS "${incs}"
	PRIV_REQUIRES "${priv_req}" 
	)
 