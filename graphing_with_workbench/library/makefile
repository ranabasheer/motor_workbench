F2806X_PATH:=C:/ti/c2000/C2000Ware_3_03_00_00/device_support/f2806x
CL2000:=C:/ti/ccs1020/ccs/tools/compiler/ti-cgt-c2000_20.2.2.LTS

CC:=$(CL2000)/bin/cl2000
AR:=$(CL2000)/bin/ar2000

OBJS:=serial_log_packet.o\
	serial_log_stream.o\
	serial_log.o
      
INCS:=--include_path=./\
	--include_path=../\
	--include_path=$(F2806X_PATH)/common/include\
	--include_path=$(F2806X_PATH)/headers/include\
	--include_path=$(CL2000)/include
 
DEFS:= --define=_FLASH_NO --define=LARGE_MODEL 
CFLAGS:= -v28 -ml -mt --cla_support=cla0 --float_support=fpu32 --vcu_support=vcu0 --diag_warning=225 --diag_wrap=off --display_error_number --abi=coffabi --preproc_with_compile 


%.o: %.c
	$(CC) --compile_only $(INCS) $(CFLAGS) --output_file=$@ --c_file=$<

libserial_log: $(OBJS)
	$(AR) -a $@ $(OBJS)
 
