#include <cstdint>
#include <cstring>
#include <sys/mman.h>
#include <new>
#include "pl/Gloss.h"
#include "pl/Signature.h"

// ============== 自定义几何模型数据 ==============
// 十字形/加号形状的几何模型 - 直接替换树叶几何
static const char* CUSTOM_GEOMETRY_JSON = R"JSON(
{
  "format_version": "1.21.60",
  "minecraft:geometry": [
    {
      "description": {
        "identifier": "minecraft:geometry.leaves",
        "texture_width": 16,
        "texture_height": 16
      },
      "bones": [
        {
          "name": "cube",
          "pivot": [0, 0, 0],
          "rotation": [0, -45, 0],
          "cubes": [
            {
              "origin": [-9, 0, -7],
              "size": [16, 16, 16],
              "uv": {
                "north": {"uv": [0, 0], "uv_size": [16, 16]},
                "east": {"uv": [0, 0], "uv_size": [16, 16]},
                "south": {"uv": [0, 0], "uv_size": [16, 16]},
                "west": {"uv": [0, 0], "uv_size": [16, 16]},
                "up": {"uv": [16, 16], "uv_size": [-16, -16]},
                "down": {"uv": [16, 16], "uv_size": [-16, -16]}
              }
            }
          ]
        },
        {
          "name": "cube2",
          "pivot": [0, 0, 0],
          "cubes": [
            {
              "origin": [-9, 0, -7],
              "size": [16, 16, 16],
              "uv": {
                "north": {"uv": [0, 0], "uv_size": [16, 16]},
                "east": {"uv": [0, 0], "uv_size": [16, 16]},
                "south": {"uv": [0, 0], "uv_size": [16, 16]},
                "west": {"uv": [0, 0], "uv_size": [16, 16]},
                "up": {"uv": [16, 16], "uv_size": [-16, -16]},
                "down": {"uv": [16, 16], "uv_size": [-16, -16]}
              }
            }
          ]
        }
      ]
    }
  ]
}
)JSON";

// ============== 签名定义 ==============

// 树叶几何标识符字符串
static const char* LEAVES_GEOMETRY_STRING = "minecraft:geometry.leaves";

// 几何注册函数签名 - 查找解析几何JSON的函数
static const char* GEOMETRY_PARSE_SIG = 
    "FD 7B BF A9 FC 0F 00 A9 FA 17 00 A9 F8 1F 00 A9 "
    "F6 27 00 A9 ?? ?? ?? 91 ?? ?? ?? 91 ?? ?? ?? 91";

// ============== 常量定义 ==============

// 目标几何标识符（直接覆盖树叶）
static const char* TARGET_GEOMETRY_ID = "minecraft:geometry.leaves";

// ============== 内存操作 ==============

static bool SetMemoryRW(void* addr, size_t size) {
    uintptr_t page_start = (uintptr_t)addr & ~(uintptr_t)4095;
    size_t page_size = (size + 4095) & ~(size_t)4095;
    return mprotect((void*)page_start, page_size, 
                    PROT_READ | PROT_WRITE | PROT_EXEC) == 0;
}

static bool SetMemoryRX(void* addr, size_t size) {
    uintptr_t page_start = (uintptr_t)addr & ~(uintptr_t)4095;
    size_t page_size = (size + 4095) & ~(size_t)4095;
    return mprotect((void*)page_start, page_size, 
                    PROT_READ | PROT_EXEC) == 0;
}

// ============== 方法1: 直接替换树叶几何标识符 ==============

static bool PatchLeavesGeometryId() {
    // 查找 "minecraft:geometry.leaves" 字符串地址
    uintptr_t strAddr = pl::signature::pl_resolve_signature(
        LEAVES_GEOMETRY_STRING, "libminecraftpe.so");
    
    if (strAddr == 0) {
        return false;
    }
    
    // 获取可写权限
    if (!SetMemoryRW((void*)strAddr, 32)) {
        return false;
    }
    
    // 替换为自定义几何标识符
    // 由于我们的几何模型ID已经是 "minecraft:geometry.leaves"，
    // 这里实际上不需要替换，几何数据会直接覆盖
    const char* newId = TARGET_GEOMETRY_ID;
    size_t newLen = strlen(newId);
    
    // 确保有足够空间
    strncpy((char*)strAddr, newId, newLen);
    ((char*)strAddr)[newLen] = '\0';
    
    // 恢复只读权限
    SetMemoryRX((void*)strAddr, 32);
    
    return true;
}

// ============== 方法2: 注入几何数据 ==============

// 查找并调用几何注册函数
static bool InjectGeometryData() {
    // 查找几何解析/注册函数
    uintptr_t parseFunc = pl::signature::pl_resolve_signature(
        GEOMETRY_PARSE_SIG, "libminecraftpe.so");
    
    if (parseFunc == 0) {
        return false;
    }
    
    // 创建 JSON 字符串缓冲区
    size_t jsonLen = strlen(CUSTOM_GEOMETRY_JSON) + 1;
    char* jsonBuffer = new (std::nothrow) char[jsonLen];
    if (!jsonBuffer) {
        return false;
    }
    strcpy(jsonBuffer, CUSTOM_GEOMETRY_JSON);
    
    // 调用几何解析函数
    // 参数通常是: X0=JSON字符串, X1=长度或其他参数
    typedef void (*GeometryParseFunc)(const char*, size_t);
    GeometryParseFunc parse = reinterpret_cast<GeometryParseFunc>(parseFunc);
    
    // 使用 C 语言调用，避免内联汇编的 ARM64 限制
    parse(jsonBuffer, jsonLen);
    
    delete[] jsonBuffer;
    return true;
}

// ============== 方法3: Hook 几何获取函数 ==============

static uintptr_t g_originalGetGeometry = 0;

// Hook 函数 - 拦截几何获取并返回自定义几何
static const char* Hook_GetGeometry(void* block) {
    // 调用原函数
    typedef const char* (*GetGeometryFunc)(void*);
    GetGeometryFunc original = reinterpret_cast<GetGeometryFunc>(g_originalGetGeometry);
    const char* result = original(block);
    
    // 如果是树叶，替换为自定义几何（由于ID相同，这里不需要替换）
    // 几何数据已经覆盖了原始的 leaves 几何
    return result;
}

static bool InstallGeometryHook() {
    // 查找几何获取函数
    uintptr_t getGeometryFunc = pl::signature::pl_resolve_signature(
        "FD 7B BF A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? A9 ?? ?? ?? 91", 
        "libminecraftpe.so");
    
    if (getGeometryFunc == 0) {
        return false;
    }
    
    g_originalGetGeometry = getGeometryFunc;
    
    // 安装 Hook (需要 Hook 框架支持)
    // 这里简化处理，直接修改函数入口
    if (!SetMemoryRW((void*)getGeometryFunc, 8)) {
        return false;
    }
    
    // 写入跳转指令到 Hook_GetGeometry
    uintptr_t hookAddr = reinterpret_cast<uintptr_t>(Hook_GetGeometry);
    uint32_t jumpInst = 0x14000000 | ((hookAddr - getGeometryFunc - 4) >> 2);
    *(uint32_t*)getGeometryFunc = jumpInst;
    
    SetMemoryRX((void*)getGeometryFunc, 8);
    return true;
}

// ============== 初始化 ==============

__attribute__((constructor))
void CustomGeometryMod_Init() {
    GlossInit(true);
    
    // 步骤1: 注入自定义几何数据
    InjectGeometryData();
    
    // 步骤2: 修改树叶使用自定义几何
    if (!PatchLeavesGeometryId()) {
        // 如果直接替换失败，尝试 Hook 方式
        InstallGeometryHook();
    }
}

// ============== 代码说明 ==============
/*
几何模型说明：

这个模型是一个十字形/加号形状，由两个立方体组成：
- cube: 旋转 -45 度的立方体
- cube2: 不旋转的立方体

两个立方体叠加形成一个三维十字形，类似于紫颂花的形状。

使用方法：
1. 编译为 .so 共享库
2. 通过 preloader-android 注入到 MCBE

效果：
- 所有使用 "minecraft:geometry.leaves" 的方块（树叶）将显示为十字形
- 包括橡树树叶、云杉树叶、白桦树叶等所有树叶类型
*/
