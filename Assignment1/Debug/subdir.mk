################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../mbsh.c 

OBJS += \
./mbsh.o 

C_DEPS += \
./mbsh.d 


# Each subdirectory must supply rules for building sources it contributes
mbsh.o: ../mbsh.c
	@echo 'Building file: $<'
	@echo 'Invoking: GCC C Compiler'
	gcc -O0 -g3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"mbsh.d" -o"$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


