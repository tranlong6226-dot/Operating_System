################################################################################
# Automatically-generated file. Do not edit!
# Toolchain: GNU Tools for STM32 (14.3.rel1)
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../mylib/rtsfr_detect.c \
../mylib/rtsfr_resource.c 

OBJS += \
./mylib/rtsfr_detect.o \
./mylib/rtsfr_resource.o 

C_DEPS += \
./mylib/rtsfr_detect.d \
./mylib/rtsfr_resource.d 


# Each subdirectory must supply rules for building sources it contributes
mylib/%.o mylib/%.su mylib/%.cyclo: ../mylib/%.c mylib/subdir.mk
	arm-none-eabi-gcc "$<" -mcpu=cortex-m3 -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB -c -I../Core/Inc -I"/home/long/Logic_Analyzer/Publisher/Resource_/mylib" -I../Drivers/STM32F1xx_HAL_Driver/Inc -I../Drivers/STM32F1xx_HAL_Driver/Inc/Legacy -I../Middlewares/Third_Party/FreeRTOS/Source/include -I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 -I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM3 -I../Drivers/CMSIS/Device/ST/STM32F1xx/Include -I../Drivers/CMSIS/Include -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -fcyclomatic-complexity -MMD -MP -MF"$(@:%.o=%.d)" -MT"$@" --specs=nano.specs -mfloat-abi=soft -mthumb -o "$@"

clean: clean-mylib

clean-mylib:
	-$(RM) ./mylib/rtsfr_detect.cyclo ./mylib/rtsfr_detect.d ./mylib/rtsfr_detect.o ./mylib/rtsfr_detect.su ./mylib/rtsfr_resource.cyclo ./mylib/rtsfr_resource.d ./mylib/rtsfr_resource.o ./mylib/rtsfr_resource.su

.PHONY: clean-mylib

