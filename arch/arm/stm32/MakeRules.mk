# ------------------------------------------------------------------------------
# STM32 Makefile Template by Pieter Conradie <https://piconomix.com>
#
# Based on:
# - WinAVR Makefile Template written by Eric B. Weddington, J�rg Wunsch, et al.
# - WinARM template makefile by Martin Thomas, Kaiserslautern, Germany 
# - DMBS - Dean's Makefile Build System by Dean Camera [https://github.com/abcminiuser/dmbs]
# - http://stackoverflow.com Q&A research
# - Automatically generate makefile dependencies (http://www.microhowto.info/howto/automatically_generate_makefile_dependencies.html)
#
# Makefile script special requirements:
# - make 3.81 or greater (requires working eval function)
# - rm (to delete files and directories)
#
# ------------------------------------------------------------------------------
# Creation Date:  2018-03-17
# ------------------------------------------------------------------------------

# Define programs and commands
CPREFIX   = arm-none-eabi
CC        = $(CPREFIX)-gcc
OBJCOPY   = $(CPREFIX)-objcopy
OBJDUMP   = $(CPREFIX)-objdump
SIZE      = $(CPREFIX)-size
NM        = $(CPREFIX)-nm
DEBUGGER  = $(CPREFIX)-gdb
REMOVE    = rm -f
REMOVEDIR = rm -rf
COPY      = cp

# Define Messages
MSG_BEGIN               = '-------- begin --------'
MSG_END                 = '--------  end  --------'
MSG_SIZE                = 'Size:'
MSG_FLASH_HEX           = 'Creating HEX load file for Flash:'
MSG_FLASH_BIN           = 'Creating BIN load file for Flash:'
MSG_EEPROM              = 'Creating load file for EEPROM:'
MSG_EXTENDED_LISTING    = 'Creating Extended Listing:'
MSG_SYMBOL_TABLE        = 'Creating Symbol Table:'
MSG_LINKING             = 'Linking:'
MSG_COMPILING           = 'Compiling C:'
MSG_COMPILING_CPP       = 'Compiling C++:'
MSG_ASSEMBLING          = 'Assembling:'
MSG_CLEANING            = 'Cleaning project:'
MSG_OPENOCD_GDB_SERVER  = 'Launching OpenOCD GDB server:'
MSG_GDB                 = 'Launching GDB:'

# Define all object files from source files (C, C++ and Assembly)
#     Relative paths are stripped and OBJDIR prefix is added.
OBJECTS = $(foreach file,$(SRC) $(CPPSRC) $(ASRC), $(OBJDIR)/$(basename $(notdir $(file))).o)

# Compiler flags to generate dependency files
GENDEPFLAGS = -MMD -MP -MF $$(patsubst %.o,%.d,$$@)

# Add DEFINE macros (with -D prefixed) to compiler flags
CFLAGS   += $(addprefix -D,$(CDEFS))
CPPFLAGS += $(addprefix -D,$(CPPDEFS))
AFLAGS   += $(addprefix -D,$(ADEFS))

# Combine all necessary flags and optional flags
#     Add target processor to flags
#     By using ?= assignment operator, these variables can be overided in 
#     Makefile that includes this boilerplate file
ALL_CFLAGS   ?= $(MCU) $(CFLAGS) -Wa,-adhlns=$$(patsubst %.o,%.lst,$$@) -I. $(addprefix -I,$(INCDIRS)) $(GENDEPFLAGS)
ALL_CPPFLAGS ?= $(MCU) -x c++ $(CPPFLAGS) -Wa,-adhlns=$$(patsubst %.o,%.lst,$$@) -I. $(addprefix -I,$(INCDIRS)) $(GENDEPFLAGS)
ALL_ASFLAGS  ?= $(MCU) -x assembler-with-cpp $(AFLAGS) -I. $(addprefix -I,$(INCDIRS)) -Wa,-adhlns=$$(patsubst %.o,%.lst,$$@),--listing-cont-lines=100,--gstabs
ALL_LDFLAGS  ?= $(MCU) $(LDFLAGS) -Wl,-Map=$(OBJDIR)/$(PROJECT).map $(addprefix -L,$(EXTRALIBDIRS))

# Default target
all: begin gccversion build size end

# Build targets
build: elf hex bin lss sym

elf: $(PROJECT).elf
hex: $(PROJECT).hex
bin: $(PROJECT).bin
lss: $(PROJECT).lss
sym: $(PROJECT).sym

# Default target to create specified source files.
# If this rule is executed then the input source file does not exist.
$(SRC) $(CPPSRC) $(ASRC):
	$(error "Source file does not exist: $@")

# Check that specified Object files directory is not empty or the current directory
ifeq ($(strip $(OBJDIR)),)
    $(error "OBJDIR must not be empty or a space")
else ifeq ($(strip $(OBJDIR)),.)
    $(error "OBJDIR must not be a .")
endif

# Eye candy
begin:
	@echo
	@echo $(MSG_BEGIN)

end:
	@echo $(MSG_END)
	@echo

# Display size of file
size: $(PROJECT).elf
	@echo
	@echo $(MSG_SIZE)
	$(SIZE) $(PROJECT).elf

# Display compiler version information
gccversion : 
	@$(CC) --version

# Create final output file (.hex) from ELF output file
%.hex: %.elf
	@echo
	@echo $(MSG_FLASH_HEX) $@
	$(OBJCOPY) -O ihex $< $@

# Create final output file (.bin) from ELF output file
%.bin: %.elf
	@echo
	@echo $(MSG_FLASH_BIN) $@
	$(OBJCOPY) -O binary $< $@

# Create extended listing file from ELF output file
%.lss: %.elf
	@echo
	@echo $(MSG_EXTENDED_LISTING) $@
	$(OBJDUMP) -h -S -z $< > $(OBJDIR)/$@

# Create a symbol table from ELF output file
%.sym: %.elf
	@echo
	@echo $(MSG_SYMBOL_TABLE) $@
	$(NM) -n $< > $(OBJDIR)/$@

# Link: create ELF output file from object files
.SECONDARY : $(PROJECT).elf
.PRECIOUS : $(OBJECTS)
%.elf: $(OBJECTS)
	@echo
	@echo $(MSG_LINKING) $@
	$(CC) $(ALL_LDFLAGS) $^ -o $@

# Compile: create object files from C source files
#   A separate rule is created for each SRC file to ensure that the correct
#   file is used to create the object file (in the OBJDIR directory).
define create_c_obj_rule
$(OBJDIR)/$(basename $(notdir $(1))).o: $(1)
	@echo
	@echo $(MSG_COMPILING) $$<
	$(CC) -c $(ALL_CFLAGS) $$< -o $$@
endef
$(foreach file,$(SRC),$(eval $(call create_c_obj_rule,$(file)))) 

# Compile: create object files from C++ source files
#   A separate rule is created for each CPPSRC file to ensure that the correct
#   file is used to create the object file (in the OBJDIR directory).
define create_cpp_obj_rule
$(OBJDIR)/$(basename $(notdir $(1))).o: $(1)
	@echo
	@echo $(MSG_COMPILING_CPP) $$<
	$(CC) -c $(ALL_CPPFLAGS) $$< -o $$@ 
endef
$(foreach file,$(CPPSRC),$(eval $(call create_cpp_obj_rule,$(file)))) 

# Assemble: create object files from assembler source files
#   A separate rule is created for each ASRC file to ensure that the correct
#   file is used to create the object file (in the OBJDIR directory).
define create_asm_obj_rule
$(OBJDIR)/$(basename $(notdir $(1))).o: $(1)
	@echo
	@echo $(MSG_ASSEMBLING) $$<
	$(CC) -c $(ALL_ASFLAGS) $$< -o $$@
endef
$(foreach file,$(ASRC),$(eval $(call create_asm_obj_rule,$(file)))) 

# Target: clean project
clean: begin clean_list end

clean_list :
	@echo
	@echo $(MSG_CLEANING)
	$(REMOVE) $(PROJECT).hex
	$(REMOVE) $(PROJECT).bin
	$(REMOVE) $(PROJECT).eep
	$(REMOVE) $(PROJECT).elf
	$(REMOVE) $(OPENOCD_SCRIPT)
	$(REMOVE) $(GDB_SCRIPT)
	$(REMOVEDIR) $(OBJDIR)

# Generate OpenOCD config file
.PHONY : $(OPENOCD_SCRIPT)
$(OPENOCD_SCRIPT):
	@echo 'gdb_port $(GDB_PORT)' >> $(OPENOCD_SCRIPT)
	@echo 'source [find $(OPENOCD_INTERFACE)]' >> $(OPENOCD_SCRIPT)
	@echo 'transport select hla_swd' >> $(OPENOCD_SCRIPT)
	@echo 'source [find $(OPENOCD_TARGET)]' >> $(OPENOCD_SCRIPT)
	@echo 'reset_config srst_only' >> $(OPENOCD_SCRIPT)
	@echo '$$_TARGETNAME configure -event gdb-attach {' >> $(OPENOCD_SCRIPT)
	@echo '   halt' >> $(OPENOCD_SCRIPT)
	@echo '}' >> $(OPENOCD_SCRIPT)
	@echo '$$_TARGETNAME configure -event gdb-attach {' >> $(OPENOCD_SCRIPT)
	@echo '   reset init' >> $(OPENOCD_SCRIPT)
	@echo '}' >> $(OPENOCD_SCRIPT)

# Start OpenOCD GDB Server
openocd: $(OPENOCD_SCRIPT)
	@echo $(MSG_OPENOCD_GDB_SERVER)
	@openocd --version
	openocd --file $(GDBSERVER_SCRIPT)

# Generate GDB config/init file
$(GDB_SCRIPT) : 
	@echo '# Load program to debug' >> $(GDB_SCRIPT)
	@echo 'file $(PROJECT).elf' >> $(GDB_SCRIPT)
	@echo '# Connect to GDB server' >> $(GDB_SCRIPT)
	@echo 'target extended-remote localhost:$(GDB_PORT)' >> $(GDB_SCRIPT)
	@echo '# Execute a reset and halt of the target' >> $(GDB_SCRIPT)
	@echo 'monitor reset halt' >> $(GDB_SCRIPT)
	@echo '# Flash the program to the target Flash memory' >> $(GDB_SCRIPT)
	@echo 'load' >> $(GDB_SCRIPT)
	@echo '# Add a one-time break point at main' >> $(GDB_SCRIPT)
	@echo 'thbreak main' >> $(GDB_SCRIPT)
	@echo '# Run the program until the break point' >> $(GDB_SCRIPT)
	@echo 'continue' >> $(GDB_SCRIPT)

# Start GDB debugging
gdb: $(GDB_SCRIPT) elf
	@echo $(MSG_GDB)
	$(DEBUGGER) --command=$(GDB_SCRIPT)

# Create object file directory
$(shell mkdir $(OBJDIR) 2>/dev/null)

# Include the compiler generated dependency files
-include $(OBJECTS:%.o=%.d)

# Listing of phony targets
.PHONY : all begin finish end sizebefore sizeafter gccversion \
build elf hex bin lss sym clean clean_list openocd gdb

