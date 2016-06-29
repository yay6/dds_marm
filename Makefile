PREFIX ?= arm-none-eabi

CC     := $(PREFIX)-gcc
LD     := $(PREFIX)-ld
AR     := $(PREFIX)-ar
AS     := $(PREFIX)-as

PROJECT ?= dds_marm

CFLAGS += -march=armv7e-m -mtune=cortex-m4 -mthumb
LDFLAGS += -TLinkerScript.ld -g -gdwarf-2
#LDFLAGS += -Map=$(PROJECT).map


OUT ?= marm_dds.bin

# StdPeriph_Driver
StdPeriph_PATH := lib/STM32F4xx_StdPeriph_Driver/

StdPeriph_SRC  := src/misc.c                \
			 	  src/stm32f4xx_dac.c    \
			 	  src/stm32f4xx_dma.c    \
			 	  src/stm32f4xx_exti.c    \
			 	  src/stm32f4xx_gpio.c   \
			 	  src/stm32f4xx_rcc.c    \
			 	  src/stm32f4xx_syscfg.c \
			 	  src/stm32f4xx_sdio.c \
			 	  src/stm32f4xx_usart.c \
			 	  src/stm32f4xx_fsmc.c \
			 	  src/stm32f4xx_tim.c
StdPeriph_HDR := $(wildcard $(StdPeriph_PATH)/inc/*.h)

OBJ += $(addprefix $(StdPeriph_PATH), $(StdPeriph_SRC:.c=.o))
CPPFLAGS += -I$(StdPeriph_PATH)/inc
CPPFLAGS += $(addprefix -include, $(StdPeriph_HDR))

# ETH_Driver
ETH_Driver_PATH := lib/STM32F4x7_ETH_Driver/

OBJ += $(ETH_Driver_PATH)/src/stm32f4x7_eth.o
CPPFLAGS += -I$(ETH_Driver_PATH)/inc

# CMIS
CMIS_PATH = lib/CMSIS/
#OBJ += $(CMIS_PATH)/Device/ST/STM32F4xx/Source/Templates/arm/startup_stm32f4xx.o
#OBJ += $(CMIS_PATH)/Device/ST/STM32F4xx/Source/Templates/system_stm32f4xx.o
CPPFLAGS += -I$(CMIS_PATH)/Device/ST/STM32F4xx/Include  \
            -I$(CMIS_PATH)/Include

# startup
OBJ += startup/startup_stm32f407xx.o

# LwIP
LwIP_PATH := utils/lwip_v1.3.2/

CPPFLAGS += -I$(LwIP_PATH)/src/include                  \
	        -I$(LwIP_PATH)/src/include/ipv4             \
	 	    -I$(LwIP_PATH)/port/STM32F4x7/Standalone    \
	        -I$(LwIP_PATH)/port/STM32F4x7

LwIP_SRC := src/api/api_lib.c 							\
		    src/api/api_msg.c 							\
            src/core/ipv4/autoip.c                      \
            src/core/dhcp.c                             \
            src/core/dns.c                              \
            src/api/err.c                               \
            src/netif/etharp.c                          \
            port/STM32F4x7/Standalone/ethernetif.c      \
            src/core/ipv4/icmp.c                        \
            src/core/ipv4/igmp.c                        \
            src/core/ipv4/inet.c                        \
            src/core/ipv4/inet_chksum.c                 \
            src/core/init.c                             \
            src/core/ipv4/ip.c                          \
            src/core/ipv4/ip_addr.c                     \
            src/core/ipv4/ip_frag.c                     \
            src/netif/loopif.c                          \
            src/core/mem.c                              \
            src/core/memp.c                             \
            src/api/netbuf.c                            \
            src/api/netdb.c                             \
            src/core/netif.c                            \
            src/api/netifapi.c                          \
            src/core/pbuf.c                             \
            src/core/raw.c                              \
            src/netif/slipif.c                          \
            src/api/sockets.c                           \
            src/core/stats.c                            \
            src/core/sys.c                              \
            src/core/tcp.c                              \
            src/core/tcp_in.c                           \
            src/core/tcp_out.c                          \
            src/api/tcpip.c                             \
            src/core/udp.c

OBJ += $(addprefix $(LwIP_PATH), $(LwIP_SRC:.c=.o))

# STM32F4-Discovery
CPPFLAGS += -Iutils/STM32F4-Discovery/
OBJ += ./utils/STM32F4-Discovery/stm32f4_discovery.o
OBJ += ./utils/STM32F4-Discovery/stm32f4_discovery_lcd.o

# User files 
SRC := $(wildcard src/*.c)
OBJ += $(SRC:.c=.o)
CPPFLAGS += -Iinc -include inc/stm32f4xx_conf.h

DEP := $(SRC:.c=.d)

all: $(OUT)

$(OUT): $(OBJ) 
	$(CC) $(LDFLAGS) -o $@ $^

clean:
	@rm -f $(OUT) $(OBJ) > /dev/null

flash: $(OUT)
	st-flash write $(OUT) 0x8000000

-include $(DEP)
