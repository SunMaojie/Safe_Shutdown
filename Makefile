#---------------------------------------------------------------------------------
# SafeShutdown NRO Makefile
# Nintendo Switch Homebrew 安全关机
#
# 编译依赖: devkitPro (libnx + switch-tools)
# 编译:     make        （Windows 下推荐用 Docker devkitpro/devkita64）
# 输出:     SafeShutdown.nro  ->  SD 卡 /switch/ 目录
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "请先设置 DEVKITPRO 环境变量: export DEVKITPRO=/opt/devkitpro")
endif

TOPDIR ?= $(CURDIR)

# 图标必须在 include switch_rules 之前定义（switch_rules 用 ifeq APP_ICON 空则回退默认图）
# elf2nro 要求 256x256 JPEG
APP_ICON := $(CURDIR)/icon.jpg

include $(DEVKITPRO)/libnx/switch_rules

#---------------------------------------------------------------------------------
# 项目配置
#---------------------------------------------------------------------------------
TARGET      := SafeShutdown
APP_TITLE   := Safe Shutdown
APP_AUTHOR  := SunMaojie
APP_VERSION := 1.4.4

BUILD       := build
SOURCES     := source
INCLUDES    := include

#---------------------------------------------------------------------------------
# 编译选项
#---------------------------------------------------------------------------------
ARCH    := -march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE

CFLAGS  := -g -Wall -O2 -ffunction-sections $(ARCH) $(DEFINES)
CFLAGS  += $(INCLUDE) -D__SWITCH__ -DAPP_VERSION="\"$(APP_VERSION)\""

# 倒计时数字样式：1=# 粗块(默认) 2=@ 3==| 细七段 4=+
# 换样式：make COUNT_STYLE=2
COUNT_STYLE ?= 1
CFLAGS  += -DCOUNT_STYLE=$(COUNT_STYLE)

CXXFLAGS := $(CFLAGS) -fno-exceptions -std=c++20

ASFLAGS  := -g $(ARCH)
LDFLAGS  = -specs=$(DEVKITPRO)/libnx/switch.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS    := -lnx

LIBDIRS := $(PORTLIBS) $(LIBNX)

#---------------------------------------------------------------------------------
# 内部构建规则
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)

export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                  $(foreach dir,$(DATA),$(CURDIR)/$(dir))

export DEPSDIR := $(CURDIR)/$(BUILD)

CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES    :=

# ★ 关键：LD 必须显式 export 为交叉编译器 driver。
#   devkitPro 的 base_tools 只导出 CC/CXX/AS/AR 等，不导出 LD；
#   若不设置，switch_rules 的 %.elf 规则会调用宿主 ld，导致
#   "undefined reference to consoleInit" 之类的链接失败。
ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_SRC)
export HFILES_BIN :=

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

ifeq ($(strip $(NO_ICON)),)
export NROFLAGS += --icon=$(APP_ICON)
endif

ifeq ($(strip $(NO_NACP)),)
export NROFLAGS += --nacp=$(CURDIR)/$(TARGET).nacp
endif

.PHONY: $(BUILD) clean all install

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo "clean ..."
	@rm -fr $(BUILD) $(TARGET).nro $(TARGET).nacp $(TARGET).elf $(TARGET).npdm

# 安装到 SD 卡（Linux/macOS 挂载点示例；Windows 请手动复制）
install:
	@if [ -d "/Volumes/BOOT/switch/" ]; then \
		cp $(TARGET).nro /Volumes/BOOT/switch/$(TARGET).nro; \
		echo "已安装: /Volumes/BOOT/switch/$(TARGET).nro"; \
	else \
		echo "未找到 SD 卡挂载点，请手动复制 $(TARGET).nro 到 SD 卡 /switch/ 目录"; \
	fi

else

.PHONY: all

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).nro

# NRO 由 switch_rules 的 %.nro: %.elf 规则生成，但需要先有 .nacp
$(OUTPUT).nro: $(OUTPUT).nacp

# 只声明依赖，recipe 交给 switch_rules 的 %.elf pattern rule（内部使用 $(LD)）
$(OUTPUT).elf: $(OFILES)

$(OFILES_SRC): $(HFILES_BIN)

-include $(DEPENDS)

endif
