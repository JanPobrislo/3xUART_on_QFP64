################################################################################
# Automatically-generated file. Do not edit!
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include src/subdir.mk
-include emlib/subdir.mk
-include CMSIS/EFM32GG11B/subdir.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: 3xUART_on_QFP64.axf

# Tool invocations
3xUART_on_QFP64.axf: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: GNU ARM C Linker'
	arm-none-eabi-gcc -g3 -gdwarf-2 -mcpu=cortex-m4 -mthumb -T "3xUART_on_QFP64.ld" -Xlinker --gc-sections -Xlinker -Map="3xUART_on_QFP64.map" -mfpu=fpv4-sp-d16 -mfloat-abi=softfp --specs=nano.specs -o 3xUART_on_QFP64.axf "./src/em_cmu.o" "./src/em_gpio.o" "./src/em_usart.o" "./src/main.o" "./emlib/em_system.o" "./CMSIS/EFM32GG11B/startup_efm32gg11b.o" "./CMSIS/EFM32GG11B/system_efm32gg11b.o" "./src/em_emu.o" -Wl,--start-group -lgcc -lc -lnosys -Wl,--end-group
	@echo 'Finished building target: $@'
	@echo ' '

	@echo 'Building hex file: 3xUART_on_QFP64.hex'
	arm-none-eabi-objcopy -O ihex "3xUART_on_QFP64.axf" "3xUART_on_QFP64.hex"
	@echo ' '

	@echo 'Building bin file: 3xUART_on_QFP64.bin'
	arm-none-eabi-objcopy -O binary "3xUART_on_QFP64.axf" "3xUART_on_QFP64.bin"
	@echo ' '

	@echo 'Building s37 file: 3xUART_on_QFP64.s37'
	arm-none-eabi-objcopy -O srec "3xUART_on_QFP64.axf" "3xUART_on_QFP64.s37"
	@echo ' '

	@echo 'Running size tool'
	arm-none-eabi-size "3xUART_on_QFP64.axf" -A
	@echo ' '

# Other Targets
clean:
	-$(RM) $(EXECUTABLES)$(OBJS)$(C_DEPS) 3xUART_on_QFP64.axf
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:

-include ../makefile.targets
