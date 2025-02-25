
## generic paths
BIN_DIR        = bin
OBJ_DIR        = obj
SRC_DIR        = src
CONTRIB_DIR    = contrib
INSTALL_DIR    = install
PREINSTALL_DIR = $(INSTALL_DIR)/pre
WEB_SRC_DIR    = teddycloud_web
WEB_BUILD_DIR  = dist
WEB_DIR        = data/www/web
ZIP_DIR        = install/zip

EXECUTABLE     = $(BIN_DIR)/teddycloud$(EXEC_EXT)
LINK_LO_FILE   = $(EXECUTABLE).lo
PLATFORM      ?= linux
OPTI_LEVEL    ?= -O2

ifeq ($(OS),Windows_NT)
	SHELL_ENV ?= cmd
#	build_arch:="$(shell powershell -NoProfile -Command "$$Env:PROCESSOR_ARCHITECTURE")"
#   TODO
	build_arch:="AMD64-tbd"
	build_os_id:="windows"
else
	SHELL_ENV ?= bash
	build_arch:="$(shell uname -m)"
	build_os_id:="$(shell grep "^ID=" /etc/os-release | cut -d'=' -f2- | tr -d '"')"
endif

ifeq ($(shell getconf LONG_BIT), 64)
    build_arch_bits = 64
else
    build_arch_bits = 32
endif

ifdef RUNTIME_BASE_PATH
	CFLAGS+=-DBASE_PATH=\"$(RUNTIME_BASE_PATH)\"
endif
ifdef CONFIG_RUNTIME_BASE_PATH
	CFLAGS+=-DCONFIG_BASE_PATH=\"$(CONFIG_RUNTIME_BASE_PATH)\"
endif

ifeq ($(SHELL_ENV),cmd)
build_rawDateTime:="${shell date /t} ${shell time /t}"
else
build_rawDateTime:="${shell date "+%Y-%m-%d %H:%M:%S %z"}"
endif

GIT_DIRTY ?= 1
GIT_BUILD_TIME ?= unknown
GIT_SHORT_SHA ?= unknown
GIT_SHA ?= unknown
GIT_TAG ?= unknown
build_gitDirty:=$(shell git diff --quiet 2>/dev/null && echo '0' || echo $(GIT_DIRTY))
build_gitDateTime:="$(shell git log -1 --format=%ai 2>/dev/null || echo $(GIT_BUILD_TIME))"
build_gitShortSha:=${shell git rev-parse --short HEAD 2>/dev/null || echo $(GIT_SHORT_SHA)}
build_gitSha:=${shell git rev-parse HEAD 2>/dev/null || echo $(GIT_SHA)}
build_gitTag:=${shell git name-rev --tags --name-only $(build_gitSha) 2>/dev/null || echo $(GIT_TAG)}
build_platform:=$(PLATFORM)
build_os:="$(OS)"

CFLAGS_VERSION:=-DBUILD_GIT_IS_DIRTY=${build_gitDirty} -DBUILD_GIT_DATETIME=\"${build_gitDateTime}\" -DBUILD_RAW_DATETIME=\"${build_rawDateTime}\" -DBUILD_GIT_SHORT_SHA=\"${build_gitShortSha}\" -DBUILD_GIT_SHA=\"${build_gitSha}\" -DBUILD_GIT_TAG=\"${build_gitTag}\"
CFLAGS_VERSION+=-DBUILD_PLATFORM=\"${build_platform}\" -DBUILD_OS=\"${build_os}\" -DBUILD_OS_ID=\"${build_os_id}\" -DBUILD_ARCH=\"${build_arch}\" -DBUILD_ARCH_BITS=\"${build_arch_bits}\"

ifeq ($(build_os_id),"alpine")
ifeq ($(build_arch_bits),32)
CFLAGS_VERSION+=-DBUILD_PRIuTIME_LLU=1
endif
endif

ifeq ($(build_os_id),"ubuntu")
ifeq ($(build_arch_bits),32)
CFLAGS_VERSION+=-DBUILD_PRIuTIME_LLU=1
endif
endif

ifeq ($(build_os_id),"ubuntu")
ifeq ($(build_arch),"aarch64")
ifeq ($(build_arch_bits),64)
# Workaround AddressSanitizer: CHECK failed: sanitizer_allocator_primary64.h:131 "((kSpaceBeg)) == ((address_range.Init(TotalSpaceSize, PrimaryAllocatorName, kSpaceBeg)))" (0x500000000000, 0xfffffffffffffff4) (tid=8)
# LLM: Ubuntu's Linux kernel version 6.5.0-25 increased the number of random bits used for ASLR from 28 to 32 on 64-bit systems7.
# The AddressSanitizer library hasn't been updated to accommodate this change in the ASLR configuration7.
# This mismatch causes a CHECK failure in the sanitizer_allocator_primary64.h file, specifically at line 131.
CFLAGS_VERSION+=-DSANITIZER_CAN_USE_ALLOCATOR64=0
endif
endif
endif

build_gitTagPrefix:=$(firstword $(subst _, ,$(build_gitTag)))
ifeq ($(build_gitTagPrefix),tc)
	build_version:=$(subst ${build_gitTagPrefix}_,,${build_gitTag})
	CFLAGS_VERSION+=-DBUILD_VERSION=\"${build_version}\" 
endif

WEB_GIT_DIRTY ?= 1
WEB_GIT_BUILD_TIME ?= unknown
WEB_GIT_SHORT_SHA ?= unknown
WEB_GIT_SHA ?= unknown
WEB_GIT_TAG ?= unknown
web_gitDirty:=${shell cd $(WEB_SRC_DIR) && git diff --quiet 2>/dev/null && echo '0' || echo $(WEB_GIT_DIRTY)}
web_gitDateTime:="${shell cd $(WEB_SRC_DIR) && git log -1 --format=%ai 2>/dev/null || echo $(WEB_GIT_BUILD_TIME)}"
web_gitShortSha:=${shell cd $(WEB_SRC_DIR) && git rev-parse --short HEAD 2>/dev/null || echo $(WEB_GIT_SHORT_SHA)}
web_gitSha:=${shell cd $(WEB_SRC_DIR) && git rev-parse HEAD 2>/dev/null || echo $(WEB_GIT_SHA)}
web_gitTag:=${shell cd $(WEB_SRC_DIR) && git name-rev --tags --name-only $(web_gitSha) 2>/dev/null || echo $(WEB_GIT_TAG)}
web_gitTagPrefix:=$(firstword $(subst _, ,$(web_gitTag)))
web_version:=vX.X.X
CFLAGS_VERSION+=-DWEB_GIT_IS_DIRTY=${web_gitDirty} -DWEB_GIT_DATETIME=\"${web_gitDateTime}\" -DWEB_RAW_DATETIME=\"${web_rawDateTime}\" -DWEB_GIT_SHORT_SHA=\"${web_gitShortSha}\" -DWEB_GIT_SHA=\"${web_gitSha}\" -DWEB_GIT_TAG=\"${web_gitTag}\"
ifeq ($(web_gitTagPrefix),tcw)
	web_version:=$(subst ${web_gitTagPrefix}_,,${web_gitTag})
	CFLAGS_VERSION+=-Dweb_VERSION=\"${web_version}\" 
endif

ifeq ($(OS),Windows_NT)
	SHELL       = cmd.exe
	ECHO        = echo
	MKDIR       = mkdir 
	RM          = del
	RM_R        = rd /S /Q
	CP          = copy
	CP_R        = xcopy /E /I 
	TO_TRASH    = >NUL 2>NUL
	# special assignment to have only the backslash in the variable
	SEP         = \$(strip)
else
	MKDIR       = mkdir -p
	ECHO        = echo -e
	RM          = rm -f
	RM_R        = rm -rf
	CP          = cp
	CP_R        = cp -r
	TO_TRASH    = >/dev/null 2>&1
	SEP         = /
endif


ifeq ($(PLATFORM),linux)
	EXEC_EXT       =  
	LINK_OUT_OPT   = -o $@
	CC_OUT_OPT     = -o
	CC_IN_OPT      = -c
	OBJ_EXT        = $(OBJ_EXT)
	LINK_LO_OPT    = @$(LINK_LO_FILE)
	CC            ?= gcc
	LD             = $(CC)
	OBJ_EXT        = .o
endif

ifeq ($(PLATFORM),windows)
	EXEC_EXT       = .exe
	LINK_OUT_OPT   = /OUT:$@
	CC_OUT_OPT     = /Fo
	CC_IN_OPT      = /c
	OBJ_EXT        = .obj
	LINK_LO_OPT    = $(OBJECTS)
	OBJ_EXT        = .obj
	CPU            = x64
	ifeq ($(VCToolsVersion),)
		$(info )
		$(info   You selected windows mode, but MSVCs vcvars.bat was not started yet. )
		$(info )
		$(error   Aborting)
	endif
	CC = cl.exe
	LD = link.exe
	LFLAGS += /LIBPATH:"$(WindowsSdkDir)lib\$(WindowsSDKLibVersion)\um\$(VSCMD_ARG_TGT_ARCH)"
	LFLAGS += /LIBPATH:"$(WindowsSdkDir)lib\$(WindowsSDKLibVersion)\ucrt\$(VSCMD_ARG_TGT_ARCH)"
	LFLAGS += /LIBPATH:"$(VCToolsInstallDir)lib\$(VSCMD_ARG_TGT_ARCH)"
endif

## posix/linux specific headers/sources
HEADERS_linux = 
INCLUDES_linux = 
SOURCES_linux = \
	src/platform/platform_$(PLATFORM).c \
	src/cyclone/common/os_port_posix.c \
	cyclone/common/fs_port_posix.c 
CFLAGS_linux += -Wall
ifneq ($(NO_WARN_FAIL),1)
	CFLAGS_linux += -Werror -Wno-error=format-overflow -Wno-error=stringop-truncation -Wno-error=maybe-uninitialized -Wno-error=stringop-overflow= -Wno-error=cpp
endif
CFLAGS_linux += -ggdb
CFLAGS_linux += -DFFMPEG_DECODING
LFLAGS_linux += -pthread -lm

# for now enable extensive error checking
# Add flags for extensive error checking if NO_SANITIZERS is not set to 1
ifneq ($(NO_SANITIZERS),1)
	CFLAGS_linux += -fsanitize=undefined -fsanitize=address -fno-omit-frame-pointer 
	LFLAGS_linux += -fsanitize=undefined -fsanitize=address -static-libasan
endif


CFLAGS_linux += $(OPTI_LEVEL)

## win32 specific headers/sources
HEADERS_windows = 
INCLUDES_windows = \
	-Isrc/platform/
SOURCES_windows = \
	src/platform/platform_$(PLATFORM).c\
	src/cyclone/common/os_port_windows.c \
	src/cyclone/common/fs_port_windows.c \
	src/platform/getopt.c 
LFLAGS_windows = /DEBUG:FULL
CFLAGS_windows = /DEBUG:FULL /Zi /nologo -DWIN32 /D_UNICODE
CFLAGS_windows += -DFFMPEG_DECODING


## generic headers/sources
INCLUDES = \
	-Iinclude \
	-Iinclude/protobuf-c \
	-Isrc/proto \
	-Isrc/cyclone/common \
	-Isrc/cyclone/cyclone_tcp \
	-Icyclone/common \
	-Icyclone/cyclone_ssl \
	-Icyclone/cyclone_tcp \
	-Icyclone/cyclone_crypto \
	-Icyclone/cyclone_crypto/pkix \
	-Icyclone/cyclone_crypto/pkc \
	-Icyclone/cyclone_crypto/rng \
	-IcJSON \
	-Ifat/source \
	-Iogg/include \
	-Iogg/src \
	-Iopus/include \
	-Iopus/celt \
	-Iopus/silk \
	-Iopus/silk/float

SOURCES = \
	$(wildcard $(SRC_DIR)/*.c) \
	$(wildcard $(SRC_DIR)/proto/*.c) \
	$(CYCLONE_SOURCES) \
	$(LIBOPUS_SOURCES) \
	$(LIBOGG_SOURCES) \
	$(CJSON_SOURCES) \
	$(FAT_SOURCES)

HEADERS = \
	$(wildcard include/*.h) \
	$(CYCLONE_HEADERS) \
	$(LIBOPUS_HEADERS) \
	$(LIBOGG_HEADERS) \
	$(CJSON_HEADERS) \
	$(FAT_HEADERS)


#
# merge the platform specifics here
#
SOURCES   += $(SOURCES_$(PLATFORM))
HEADERS   += $(HEADERS_$(PLATFORM))
INCLUDES  += $(INCLUDES_$(PLATFORM))
CFLAGS    += $(CFLAGS_$(PLATFORM))
LFLAGS    += $(LFLAGS_$(PLATFORM))

FAT_SOURCES = \
	fat/source/ff.c \
	fat/source/ffsystem.c \
	fat/source/ffunicode.c

FAT_HEADERS =\
	fat/source/ff.h

CJSON_SOURCES = \
	cJSON/cJSON.c \
	cJSON/cJSON_Utils.c

CJSON_HEADERS = \
	cJSON/cJSON.h \
	cJSON/cJSON_Utils.h 

LIBOGG_SOURCES = \
	ogg/src/framing.c \
	ogg/src/bitwise.c \

include opus/silk_sources.mk
include opus/celt_sources.mk
include opus/opus_sources.mk
include opus/silk_headers.mk
include opus/celt_headers.mk
include opus/opus_headers.mk

LIBOPUS_SOURCES = \
	$(addprefix opus/,$(SILK_SOURCES)) \
	$(addprefix opus/,$(SILK_SOURCES_FLOAT)) \
	$(addprefix opus/,$(CELT_SOURCES)) \
	$(addprefix opus/,$(OPUS_SOURCES)) \
	$(addprefix opus/,$(OPUS_SOURCES_FLOAT)) 

LIBOPUS_HEADERS = \
	$(addprefix opus/,$(SILK_HEAD)) \
	$(addprefix opus/,$(CELT_HEAD)) \
	$(addprefix opus/,$(OPUS_HEAD)) \

CYCLONE_SOURCES = \
	cyclone/common/cpu_endian.c \
	cyclone/common/date_time.c \
	cyclone/common/debug.c \
	cyclone/common/path.c \
	cyclone/common/str.c \
	cyclone/cyclone_tcp/http/mime.c \
	cyclone/cyclone_tcp/http/http_client.c \
	cyclone/cyclone_tcp/http/http_client_misc.c \
	cyclone/cyclone_tcp/http/http_client_transport.c \
	cyclone/cyclone_tcp/http/http_common.c \
	cyclone/cyclone_tcp/http/http_server.c \
	cyclone/cyclone_tcp/http/http_server_misc.c \
	cyclone/cyclone_tcp/mqtt/mqtt_client.c \
	cyclone/cyclone_tcp/mqtt/mqtt_client_packet.c \
	cyclone/cyclone_tcp/mqtt/mqtt_client_misc.c \
	cyclone/cyclone_ssl/tls.c \
	cyclone/cyclone_ssl/tls_cipher_suites.c \
	cyclone/cyclone_ssl/tls_handshake.c \
	cyclone/cyclone_ssl/tls_client.c \
	cyclone/cyclone_ssl/tls_client_fsm.c \
	cyclone/cyclone_ssl/tls_client_extensions.c \
	cyclone/cyclone_ssl/tls_client_misc.c \
	cyclone/cyclone_ssl/tls_server.c \
	cyclone/cyclone_ssl/tls_server_fsm.c \
	cyclone/cyclone_ssl/tls_server_extensions.c \
	cyclone/cyclone_ssl/tls_server_misc.c \
	cyclone/cyclone_ssl/tls_common.c \
	cyclone/cyclone_ssl/tls_extensions.c \
	cyclone/cyclone_ssl/tls_certificate.c \
	cyclone/cyclone_ssl/tls_signature.c \
	cyclone/cyclone_ssl/tls_key_material.c \
	cyclone/cyclone_ssl/tls_transcript_hash.c \
	cyclone/cyclone_ssl/tls_cache.c \
	cyclone/cyclone_ssl/tls_ticket.c \
	cyclone/cyclone_ssl/tls_ffdhe.c \
	cyclone/cyclone_ssl/tls_record.c \
	cyclone/cyclone_ssl/tls_record_encryption.c \
	cyclone/cyclone_ssl/tls_record_decryption.c \
	cyclone/cyclone_ssl/tls_misc.c \
	cyclone/cyclone_crypto/hash/sha1.c \
	cyclone/cyclone_crypto/hash/sha256.c \
	cyclone/cyclone_crypto/hash/sha384.c \
	cyclone/cyclone_crypto/hash/sha512.c \
	cyclone/cyclone_crypto/mac/hmac.c \
	cyclone/cyclone_crypto/cipher/aes.c \
	cyclone/cyclone_crypto/cipher_modes/cbc.c \
	cyclone/cyclone_crypto/aead/ccm.c \
	cyclone/cyclone_crypto/aead/gcm.c \
	cyclone/cyclone_crypto/xof/keccak.c \
	cyclone/cyclone_crypto/xof/shake.c \
	cyclone/cyclone_crypto/pkc/dh.c \
	cyclone/cyclone_crypto/pkc/rsa.c \
	cyclone/cyclone_crypto/pkc/dsa.c \
	cyclone/cyclone_crypto/ecc/ec.c \
	cyclone/cyclone_crypto/ecc/ec_curves.c \
	cyclone/cyclone_crypto/ecc/ecdh.c \
	cyclone/cyclone_crypto/ecc/ecdsa.c \
	cyclone/cyclone_crypto/ecc/eddsa.c \
	cyclone/cyclone_crypto/encoding/base64.c \
	cyclone/cyclone_crypto/encoding/asn1.c \
	cyclone/cyclone_crypto/encoding/oid.c \
	cyclone/cyclone_crypto/pkix/pem_import.c \
	cyclone/cyclone_crypto/pkix/pem_export.c \
	cyclone/cyclone_crypto/pkix/pem_common.c \
	cyclone/cyclone_crypto/pkix/pem_decrypt.c \
	cyclone/cyclone_crypto/pkix/pkcs8_key_parse.c \
	cyclone/cyclone_crypto/pkix/pkcs8_key_format.c \
	cyclone/cyclone_crypto/pkix/x509_key_format.c \
	cyclone/cyclone_crypto/pkix/x509_key_parse.c \
	cyclone/cyclone_crypto/pkix/x509_cert_parse.c \
	cyclone/cyclone_crypto/pkix/x509_cert_ext_parse.c \
	cyclone/cyclone_crypto/pkix/x509_cert_validate.c \
	cyclone/cyclone_crypto/pkix/x509_cert_create.c \
	cyclone/cyclone_crypto/pkix/x509_cert_format.c \
	cyclone/cyclone_crypto/pkix/x509_cert_ext_format.c \
	cyclone/cyclone_crypto/pkix/x509_crl_parse.c \
	cyclone/cyclone_crypto/pkix/x509_crl_validate.c \
	cyclone/cyclone_crypto/pkix/x509_crl_ext_parse.c \
	cyclone/cyclone_crypto/pkix/x509_common.c \
	cyclone/cyclone_crypto/pkix/x509_sign_verify.c \
	cyclone/cyclone_crypto/pkix/x509_sign_parse.c \
	cyclone/cyclone_crypto/pkix/x509_sign_generate.c \
	cyclone/cyclone_crypto/pkix/x509_sign_format.c \
	cyclone/cyclone_crypto/kdf/hkdf.c \
	cyclone/cyclone_crypto/rng/yarrow.c

# remove cyclone sources for which modifications exist
CYCLONE_SOURCES := $(filter-out \
	cyclone/common/debug.c \
	cyclone/common/error.c \
	cyclone/cyclone_crypto/cipher/aes.c \
	cyclone/cyclone_tcp/http/http_client_transport.c \
	cyclone/cyclone_tcp/http/http_server.c \
	cyclone/cyclone_tcp/http/http_server_misc.c \
	cyclone/cyclone_ssl/tls_certificate.c \
	cyclone/cyclone_tcp/mqtt/mqtt_client_transport.c \
	, $(CYCLONE_SOURCES))

# and add modified ones
CYCLONE_SOURCES += \
	src/cyclone/common/debug.c \
	src/cyclone/common/error.c \
	src/cyclone/cyclone_crypto/mpi.c \
	src/cyclone/cyclone_crypto/cipher/aes.c \
	src/cyclone/cyclone_tcp/http/http_client_transport.c \
	src/cyclone/cyclone_tcp/http/http_server.c \
	src/cyclone/cyclone_tcp/http/http_server_misc.c \
	src/cyclone/cyclone_tcp/mqtt/mqtt_client_transport.c \
	src/cyclone/cyclone_ssl/tls_certificate.c

CFLAGS += -D GPL_LICENSE_TERMS_ACCEPTED
CFLAGS += -D TRACE_NOPATH_FILE
CFLAGS += ${CFLAGS_VERSION}
CFLAGS += $(INCLUDES)

# for opus encoder
CFLAGS += -DUSE_ALLOCA -DOPUS_BUILD
CFLAGS_linux += -Wno-error=stringop-overflow= -Wno-error=stringop-overread

THIS_MAKEFILE := $(lastword $(MAKEFILE_LIST))

# Location of your .proto files
PROTO_DIR := proto
PROTO_GEN_DIR := src/proto

# Find all .proto files in the PROTO_DIR directory
PROTO_FILES := $(wildcard $(PROTO_DIR)/*.proto)

# Get the corresponding .c and .h filenames
PROTO_C_FILES := $(patsubst $(PROTO_DIR)/%.proto, $(PROTO_GEN_DIR)/$(PROTO_DIR)/%.pb-c.c, $(PROTO_FILES))
PROTO_H_FILES := $(patsubst $(PROTO_DIR)/%.proto, $(PROTO_GEN_DIR)/$(PROTO_DIR)/%.pb-c.h, $(PROTO_FILES))

# Rule to build .c files from .proto files
$(PROTO_GEN_DIR)/$(PROTO_DIR)/%.pb-c.c $(PROTO_GEN_DIR)/$(PROTO_DIR)/%.pb-c.h: $(PROTO_DIR)/%.proto
	$(QUIET)$(ECHO) '[${GREEN}PROTO${NC} ] ${CYAN}$<${NC}'
	$(QUIET)protoc-c --c_out=$(PROTO_GEN_DIR) $<

SOURCES += $(PROTO_C_FILES)
HEADERS += $(PROTO_H_FILES)
CLEAN_FILES += $(PROTO_C_FILES) $(PROTO_H_FILES)


OBJECTS = $(foreach C,$(SOURCES),$(addprefix $(OBJ_DIR)/,$(C:.c=$(OBJ_EXT))))
CLEAN_FILES += $(OBJECTS) $(LINK_LO_FILE)

ifeq ($(OS),Windows_NT)
	CYAN=
	RED=
	YELLOW=
	GREEN=
	NC=
else
	CYAN=\033[0;36m
	RED=\033[0;31m
	YELLOW=\033[0;33m
	GREEN=\033[0;32m
	NC=\033[0m
endif

ifeq ($(VERBOSE),1)
	QUIET=
else
	QUIET=@
endif

all: check_dependencies submodules web build 

echo_info:
	$(QUIET)$(ECHO) '[ ${GREEN}PLAT${NC} ] ${CYAN}$(build_platform)${NC}'
	$(QUIET)$(ECHO) '[ ${GREEN}OSID${NC} ] ${CYAN}$(build_os_id)${NC}'
	$(QUIET)$(ECHO) '[ ${GREEN}ARCH${NC} ] ${CYAN}$(build_arch)${NC}'
	$(QUIET)$(ECHO) '[ ${GREEN}BITS${NC} ] ${CYAN}$(build_arch_bits)${NC}'

build: echo_info $(EXECUTABLE)	

ifeq ($(OS),Windows_NT)
.PHONY: check_dependencies
check_dependencies:
else
.PHONY: check_dependencies
check_dependencies:
	@which protoc-c >/dev/null || ($(ECHO) '${RED}Error:${NC} protoc-c not found. Install it using:' && \
	$(ECHO) '  ${CYAN}Ubuntu/Debian:${NC} sudo apt-get install protobuf-c-compiler' && \
	$(ECHO) '  ${CYAN}Alpine:${NC} apk add protobuf' && exit 1)
	@which gcc >/dev/null || ($(ECHO) '${RED}Error:${NC} gcc not found. Install it using:' && \
	$(ECHO) '  ${CYAN}Ubuntu/Debian:${NC} sudo apt-get install gcc' && \
	$(ECHO) '  ${CYAN}Alpine:${NC} apk add gcc' && exit 1)
	@which openssl >/dev/null || ($(ECHO) '${YELLOW}Warning:${NC} openssl not found, required for generating certificates. Install it using:' && \
	$(ECHO) '  ${CYAN}Ubuntu/Debian:${NC} sudo apt-get install openssl' && \
	$(ECHO) '  ${CYAN}Alpine:${NC} apk add openssl')
	@which faketime >/dev/null || ($(ECHO) '${YELLOW}Warning:${NC} faketime not found, required for generating certificates. Install it using:' && \
	$(ECHO) '  ${CYAN}Ubuntu/Debian:${NC} sudo apt-get install faketime' && \
	$(ECHO) '  ${CYAN}Alpine:${NC} apk add faketime')
	@which npm >/dev/null || ($(ECHO) '${YELLOW}Warning:${NC} npm not found, required for building the teddycloud_web. Install it using:' && \
	$(ECHO) '  ${CYAN}Ubuntu/Debian:${NC} sudo apt-get install npm' && \
	$(ECHO) '  ${CYAN}Alpine:${NC} apk add npm')
endif

.PRECIOUS: %/
%/:
	$(info [DIR] creating $@)
	$(shell $(MKDIR) $(subst /,$(SEP),$@) $(TO_TRASH))

.SECONDEXPANSION:
$(LINK_LO_FILE): $$(dir $$@)
	$(file >$@, $(OBJECTS) $(OBJ_ONLY_FILES) )

workdirs: certs/server/ certs/client/ config/ data/www/ data/content/ data/library/ data/www/web/ data/firmware/ data/cache/
	$(QUIET)$(ECHO) '[ ${YELLOW}DIRS${NC}  ] ${CYAN}$@${NC}'
	$(QUIET)$(CP_R) $(subst /,$(SEP),$(CONTRIB_DIR)/data/www/*) $(subst /,$(SEP),data/www/) 

.SECONDEXPANSION:
$(EXECUTABLE): $(LINK_LO_FILE) $(OBJECTS) $(HEADERS) $(THIS_MAKEFILE) workdirs | $$(dir $$@)
	$(QUIET)$(ECHO) '[ ${YELLOW}LINK${NC} ] ${CYAN}$@${NC}'
	$(QUIET)$(LD) $(LFLAGS) $(LINK_LO_OPT) $(LINK_OUT_OPT)

.SECONDEXPANSION:
$(OBJ_DIR)/%$(OBJ_EXT): %.c $(HEADERS) $(THIS_MAKEFILE) | $$(dir $$@)
	$(QUIET)$(ECHO) '[ ${GREEN}CC${NC}   ] ${CYAN}$<${NC}'
	$(QUIET)$(CC) $(CFLAGS) $(CC_IN_OPT) $< $(CC_OUT_OPT)$@

clean:
	$(QUIET)$(ECHO) '[${GREEN}CLEAN${NC} ] Deleting output files...'
	$(QUIET)$(RM) $(subst /,$(SEP),$(EXECUTABLE))
	$(QUIET)$(RM) $(foreach O,$(CLEAN_FILES),$(subst /,$(SEP),$(O)) )

.PHONY: submodules
submodules:
	$(QUIET)git submodule init
	$(QUIET)git submodule update

preinstall: clean build web_copy $(INSTALL_DIR)/ $(PREINSTALL_DIR)/
	$(QUIET)$(ECHO) '[ ${GREEN}PRE${NC}  ] Preinstall'
	$(QUIET)$(CP) $(BIN_DIR)/* $(PREINSTALL_DIR)/
	$(QUIET)$(CP_R) $(subst /,$(SEP),$(CONTRIB_DIR)/*) $(subst /,$(SEP),$(PREINSTALL_DIR)/)
	$(QUIET)cd $(PREINSTALL_DIR)/ \
		&& find . -name ".gitkeep" -type f -delete \
		&& cd -

ifeq ($(OS),Windows_NT)	
web: 
web_copy: 
else
web_version:

web_clean: 
	$(QUIET)$(ECHO) '[ ${GREEN}WEB${NC}  ] Clean TeddyCloud Web'
	$(RM_R) $(CONTRIB_DIR)/$(WEB_DIR)
		
web: web_clean
	$(QUIET)$(ECHO) '[ ${GREEN}WEB${NC}  ] Build TeddyCloud Web'
	$(QUIET) $(MKDIR) $(CONTRIB_DIR)/$(WEB_DIR)/
	$(QUIET)cd $(WEB_SRC_DIR) \
		&& npm install \
		&& npm run build \
		&& $(CP_R) $(WEB_BUILD_DIR)/* ../$(CONTRIB_DIR)/$(WEB_DIR)/ \
		&& cd -
	$(QUIET)$(ECHO) '[ ${GREEN}WEB${NC}  ] Generate TeddyCloud Web version info'
	$(QUIET)echo "{" > $(CONTRIB_DIR)/$(WEB_DIR)/web_version.json
	$(QUIET)$(foreach var,$(.VARIABLES), \
		$(if $(filter web_%,$(var)), \
			(echo '  "$(var)": "'${$(var)}'",';) >> $(CONTRIB_DIR)/$(WEB_DIR)/web_version.json; \
		) \
	)
	$(QUIET)echo "  \"_eof\":\"\"\n}" >> $(CONTRIB_DIR)/$(WEB_DIR)/web_version.json

web_copy: 
	$(QUIET)$(ECHO) '[ ${GREEN}WEB${NC}  ] Copy TeddyCloud Web'
	$(QUIET) $(MKDIR) $(PREINSTALL_DIR)/$(WEB_DIR)/
	$(QUIET) $(CP_R) $(CONTRIB_DIR)/$(WEB_DIR)/* $(PREINSTALL_DIR)/$(WEB_DIR)/ 
endif

zip: preinstall
	$(QUIET)$(ECHO) '[ ${GREEN}ZIP${NC}  ] Create release zip'
	$(QUIET) $(MKDIR) $(ZIP_DIR)/
	cd $(PREINSTALL_DIR)/ \
		&& zip -r ../../$(ZIP_DIR)/release.zip * \
		&& cd -

scan-build: clean
	$(MKDIR) report
	scan-build -o report make -j

.PHONY: auto
auto:
	$(QUIET)$(ECHO) 'Entering ${CYAN}auto rebuild mode${NC}. Press Ctrl-C to exit.'
	$(QUIET)$(ECHO) '[ ${CYAN}AUTO${NC} ] Clean up'
	$(QUIET)screen -ls | grep teddycloud_auto | awk '{print $$1}' | xargs -I % screen -X -S % quit
	$(QUIET)$(ECHO) '[ ${CYAN}AUTO${NC} ] Build'
	$(QUIET)screen -S teddycloud_auto -dm
	$(QUIET)screen -S teddycloud_auto -X screen bash -c 'valgrind $(EXECUTABLE); exec sh'
	$(QUIET)last_build_time=$$(date +%s); \
	while true; do \
		modified_time=$$(stat -c "%Y" $(SOURCES) $(HEADERS) $(PROTO_FILES) $(THIS_MAKEFILE) | sort -r | head -n 1); \
		if [ "$$modified_time" -gt "$$last_build_time" ]; then \
			screen -S teddycloud_auto -X stuff "^C"; \
			make --no-print-directory -j || echo; \
			last_build_time=$$(date +%s); \
			screen -S teddycloud_auto -X screen bash -c 'valgrind $(EXECUTABLE); exec sh'; \
		fi; \
		sleep 1; \
	done;

.PHONY: cppcheck

# Run cppcheck for static code analysis
cppcheck:
	$(QUIET)$(ECHO)  "[ ${CYAN}CHK${NC} ] Running cppcheck"
	cppcheck -j6 --enable=all --inconclusive --std=c99 --language=c --platform=unspecified --report-progress --suppress=missingIncludeSystem --xml --output-file=cppcheck.xml $(wildcard $(SRC_DIR)/*.c) $(INCLUDES) -D GPL_LICENSE_TERMS_ACCEPTED -D TRACE_NOPATH_FILE
	cppcheck-htmlreport --file=cppcheck.xml --report-dir=cppcheck
