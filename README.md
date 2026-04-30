# ESP-IDF + ST7735 (128x160) 俄罗斯方块示例

这是一个使用 **C 语言**、基于 **ESP-IDF** 的俄罗斯方块最小可运行示例，目标屏幕为 **ST7735**，分辨率 **128x160**。

## 功能说明

- SPI 驱动 ST7735（RGB565）
- 10x13 俄罗斯方块棋盘
- 7 种方块与旋转
- 消行逻辑
- 当前示例为“自动演示模式”（自动下落 + 简单左右移动/旋转）

> 你可以很容易在 `game_task()` 里接入按键扫描，将自动逻辑替换成人工控制。

## 默认引脚（可改）

在 `main/tetris_game.c` 顶部宏定义中配置：

- `MOSI(SDA) = GPIO2`
- `SCLK(SCL) = GPIO15`
- `CS        = GPIO17`
- `DC        = GPIO16`
- `RST  = GPIO4`
- `BCKL(BLK) = GPIO5`

## 构建与烧录

```bash
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### 如果提示 `IDF_PATH` 未设置

你需要先安装并激活 ESP-IDF 环境。最常见方式如下（Linux/macOS）：

```bash
git clone --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf
./install.sh esp32
. ./export.sh
```

然后在同一个终端中回到本项目再执行：

```bash
idf.py set-target esp32
idf.py build
```

如果你希望每次打开终端都自动可用，可把下面这行加到 `~/.bashrc`：

```bash
source ~/esp/esp-idf/export.sh
```

> 说明：`export.sh` 会自动设置 `IDF_PATH`、Python 虚拟环境和工具链路径，通常不需要你手动 `export IDF_PATH=...`。

## 目录结构

- `CMakeLists.txt`：工程入口
- `main/CMakeLists.txt`：组件注册
- `main/tetris_game.c`：ST7735 驱动 + 俄罗斯方块逻辑



## 注意事项

1. 部分 ST7735 模块需要设置 `MADCTL` 的方向位，请按你的屏幕方向调整。
2. 若画面颜色异常，通常是字节序或颜色格式设置问题（`COLMOD=0x55` 对应 RGB565）。
3. 若你使用 ESP32-S3/ESP32-C3，SPI Host 与引脚可按芯片资源调整。

<img width="750" height="563" alt="Image" src="https://github.com/user-attachments/assets/35c76c67-3722-45bb-939b-76465f05951a" />