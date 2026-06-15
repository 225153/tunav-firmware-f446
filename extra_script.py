Import("env")

# Add FPU flags needed by ARM_CM4F FreeRTOS port
env.Append(
    CCFLAGS=["-mfpu=fpv4-sp-d16", "-mfloat-abi=softfp"],
    CPPPATH=[
        "tunav/Middlewares/Third_Party/FreeRTOS/Source/include",
        "tunav/Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2",
        "tunav/Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F",
    ]
)

# Add FreeRTOS source files
env.BuildSources(
    "$BUILD_DIR/FreeRTOS",
    "tunav/Middlewares/Third_Party/FreeRTOS/Source",
    src_filter=[
        "+<*.c>",
        "+<CMSIS_RTOS_V2/*.c>",
        "+<portable/GCC/ARM_CM4F/*.c>",
        "+<portable/MemMang/heap_4.c>",
        "-<portable/MemMang/heap_1.c>",
        "-<portable/MemMang/heap_2.c>",
        "-<portable/MemMang/heap_3.c>",
        "-<portable/MemMang/heap_5.c>",
    ]
)
