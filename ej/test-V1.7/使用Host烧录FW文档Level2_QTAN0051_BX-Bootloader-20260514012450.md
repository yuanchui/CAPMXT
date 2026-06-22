## Bootloading Procedure for Atmel® Touch Sensors Based on the Object Protocol

## 1. Introduction

This document describes how to update the firmware on Atmel touch sensor controllers that are based on the Object Protocol.

Note: After updating the firmware the host should rewrite any stored settings used by the application.

## 2. Entering the Bootloader Mode

### 2.1 Introduction

Bootloader mode is entered automatically on power-up if the firmware on the device is missing or corrupt.

If the firmware is valid and the device is running in application mode, it can be put into bootloader mode using one of the three force-flash sequences:

- Writing 0xA5 to the command processor object's RESET field (see Section 2.2)

- Using the \( \overline{\mathrm{{CHG}}} \) and RESET line together (see Section 2.3)

- Toggling the RESET line (see Section 2.4)

### 2.2 Command Processor Force-flash Sequence

Write 0xA5 to the Command Processor Object's RESET field to enter the bootloader mode. Refer to the Protocol Guide for your device for information on how to do this.

### 2.3 CHG and RESET Force-flash Sequence

With this sequence the CHG line is held low while the chip is powered up after a reset (see Figure 2-1).

Figure 2-1. CHG and RESET Force-flash Sequence

<img src="https://cdn.noedgeai.com/bo_d82amkqlb0pc73cnab4g_0.jpg?x=276&y=1421&w=651&h=534&r=0"/>

AMEL

Bootloading Procedure

## Application Note QTAN0051

---

REFLASH AGREEMENT REQUIRED

---

### 2.4 RESET Toggling Force-flash Sequence

With this sequence the RESET line is asserted ten times in a row without communicating via the \( {\mathrm{I}}^{2}\mathrm{C} \) -compatible bus between the resets.

Figure 2-2. RESET Toggling Force-flash Sequence

<img src="https://cdn.noedgeai.com/bo_d82amkqlb0pc73cnab4g_1.jpg?x=466&y=375&w=1035&h=445&r=0"/>

### 2.5 Confirmation of Bootloader Mode

When it receives a reset command, the device resets itself and starts up in bootloader mode.

The \( {\mathrm{I}}^{2}\mathrm{C} \) -compatible slave address is different to that used in application mode (see Table 3-1 on page 3).

The host can confirm that the device is in bootloader mode (WAITING_BOOTLOAD_CMD state; see Table 3-3 on page 5) by reading a status byte from the bootloader address. If the read is successful, bits 6 and 7 of the status byte returned will be set, and the other 6 bits will contain the bootloader version (see WAITING_BOOTLOAD_CMD state in Table 3-3 on page 5).

In application mode, if no application is programmed, or the application is corrupt, the CHG line will be set low. The host needs to perform an \( {\mathrm{I}}^{2}\mathrm{C} \) -compatible read to retrieve the error code and set the device into bootloader mode.

## 3. Updating the Application

### 3.1 Unlocking the Device

The device first needs to be placed in bootloader mode using one of the force-flash sequences described in Section 2 to unlock the device and start bootloading a new application.

Once the device is in bootloader mode, the host needs to write the Application Update Unlock Command (0xDC followed by 0xAA) to the device to unlock it (see Figure 3-1).

Figure 3-1. Application Update Unlock Command

<img src="https://cdn.noedgeai.com/bo_d82amkqlb0pc73cnab4g_1.jpg?x=614&y=1728&w=735&h=57&r=0"/>

2 Bootloading Procedure

### 3.2 Application Update Mode

#### 3.2.1 Introduction

In application update mode, the device accepts multibyte update commands when written to over the \( {\mathrm{I}}^{2}\mathrm{C} \) -compatible interface,and returns a single status byte when it is read.

The device responds to the \( {\mathrm{I}}^{2}\mathrm{C} \) -compatible address selected by the address pins. See Section 3.2.3 and Section 3.2.4 for more details.

#### 3.2.2 CHG line

The device indicates a transition between the states by asserting the CHG line (that is, by setting it low). Once the status byte has been read, the device releases the CHG line (that is, the device floats the \( \overline{\mathrm{{CHG}}} \) line and then the pullup returns it high). It then runs the processes associated with that state. The device waits until the status is read, so that the host cannot miss a state transition.

Note: It is very important that the host checks the status of the CHG line in order to know when it needs to read the device. The host should not simply poll the \( {\mathrm{I}}^{2}\mathrm{C} \) -compatible bus,as \( {\mathrm{I}}^{2}\mathrm{C} \) -compatible reads or writes before the \( \overline{\mathrm{{CHG}}} \) line is asserted are not supported.

#### 3.2.3 Single Device Addresses

In the case of a single maXTouch device (such as the mXT224, mXT165 and mXT140) the device responds to the \( {\mathrm{I}}^{2}\mathrm{C} \) -compatible address selected by the ADDR_SEL pin (see Table 3-1).

Table 3-1. \( \;{\mathrm{I}}^{2}\mathrm{C} \) -compatible Address Selection

<table><tr><td>ADDR_SEL</td><td>Mode</td><td>I2C-compatible Address</td></tr><tr><td>Low</td><td rowspan="2">Application</td><td>0x4A</td></tr><tr><td>High</td><td>0x4B</td></tr><tr><td>Low</td><td rowspan="2">Bootloader</td><td>0x24</td></tr><tr><td>High</td><td>0x25</td></tr></table>

#### 3.2.4 Chip Set Device Addresses

In the case of maXTouch chip set devices (such as the mXT1386 Chip Set) the device responds to the \( {\mathrm{I}}^{2}\mathrm{C} \) -compatible address selected by the A1 and A0 pins (see Table 3-2).

Table 3-2. \( {\mathrm{I}}^{2}\mathrm{C} \) -compatible Address Selection

<table><tr><td>A1</td><td>A0</td><td>Mode</td><td>IC-compatible Address</td></tr><tr><td>0</td><td>0</td><td rowspan="4">Application</td><td>0x4C</td></tr><tr><td>0</td><td>1</td><td>0x4D</td></tr><tr><td>1</td><td>0</td><td>0x5A</td></tr><tr><td>1</td><td>1</td><td>0x5B</td></tr><tr><td>0</td><td>0</td><td rowspan="4">Bootloader</td><td>0x26</td></tr><tr><td>0</td><td>1</td><td>0x27</td></tr><tr><td>1</td><td>0</td><td>0x34</td></tr><tr><td>1</td><td>1</td><td>0x35</td></tr></table>

<img src="https://cdn.noedgeai.com/bo_d82amkqlb0pc73cnab4g_3.jpg?x=759&y=54&w=171&h=81&r=0"/>

#### 3.2.5 State Machine

The application update mode works as a state machine as shown in Figure 3-2 on page 4.

Figure 3-2. Application Update Mode Program Flow

<img src="https://cdn.noedgeai.com/bo_d82amkqlb0pc73cnab4g_3.jpg?x=118&y=372&w=1428&h=1545&r=0"/>

## Bootloading Procedure

Table 3-3 describes the behavior of each state.

Table 3-3. Application Update Mode Status Codes

<table><tr><td>State</td><td>Data Transmission</td><td>Description</td></tr><tr><td>0b11nnnnnn</td><td>Yes</td><td>WAITING_BOOTLOAD_CMD: <br> The data returned by the device has bit 7 and 6 set. The remaining 6 least significant bits are part dependent and provide information about the bootloader version (see Section 3.3 on page 6). <br> In this state, the device is awaiting the bootloader command. Sending any other byte than the bootloader command causes the device to leave bootloader mode.</td></tr><tr><td>0b10nnnnnn</td><td>Yes</td><td>WAITING_FRAME_DATA: <br> The data returned by the QT device has bit 7 set. The remaining 6 least significant bits are part dependent and provide information about the bootloader version (see Section 3.3 on page 6). <br> In this state, the device is awaiting frame data from the host. Once it has received a whole frame it moves to the CRC check state.</td></tr><tr><td>0b00000010</td><td>No</td><td>FRAME_CRC_CHECK: <br> The host does nothing at this stage but waits for the CRC check result.</td></tr><tr><td>0b00000011</td><td>No</td><td>FRAME_CRC_FAIL: <br> If the state machine reaches this state, the CRC check for the current frame has failed. <br> The host should try resending the last frame once the device returns to the WAITING_FRAME_DATA state.</td></tr><tr><td>0b00000100</td><td>No</td><td>FRAME_CRC_PASS: <br> If the state machine reaches this state, the current frame data has passed the CRC check, and the device is proceeding to process the frame records. <br> The host should wait until the device returns to the WAITING_FRAME_DATA state before sending the next frame.</td></tr><tr><td>0b01nnnnnn</td><td>No</td><td>APP_CRC_FAIL: <br> The data returned by the device has bit 6 set. The remaining 6 least significant bits are part dependent and provide information about the bootloader version (see Section 3.3 on page 6). <br> If the state machine reaches this state, the CRC check for the currently stored application code failed. <br> The host can make the device enter bootloader mode to recover the firmware by sending the bootloader command sequence.</td></tr></table>

### 3.3 Bootloader Version Information

Some of the states return the bootloader version information within their status codes. Table 3-4 shows how to decode this information.

Table 3-4. Bootloader Version Information

<table><tr><td colspan="4">Status Byte</td><td rowspan="2">Description</td></tr><tr><td>Bit 7</td><td>Bit 6</td><td>Bit 5</td><td>Bits 4..0</td></tr><tr><td>X</td><td>X</td><td>0</td><td>Bootloader ID</td><td>Bit 5 of the status byte is set to 0: <br> Bits 0..4 contain a 5-bit bootloader ID for the device (for details of the ID, refer to the Protocol guide for your device).</td></tr><tr><td>X</td><td>X</td><td>1</td><td>Reserved</td><td>Bit 5 of the status byte is set to 1: <br> The bootloader ID is read by reading two additional bytes. See Figure 3-3.</td></tr></table>

To read the bootloader ID from a bootloader that uses the extended ID mode, a 3-byte \( {I}^{2}C \) -compatible read must first be performed (see Figure 3-3):

1. The first byte is the status byte with bit 5 set to 1 to indicate extended ID mode.

2. The second byte is the bootloader ID code.

3. The third byte is the bootloader version code.

Note that this three-byte read is necessary only once to obtain the bootloader ID. The rest of the application update process may be performed using single-byte reads.

Figure 3-3. Extended ID Mode

<img src="https://cdn.noedgeai.com/bo_d82amkqlb0pc73cnab4g_5.jpg?x=486&y=1117&w=1026&h=173&r=0"/>

### 3.4 Data Frames

The application firmware is sent to the device as frames of data. Each frame (see Figure 3-4) consists of:

- A 2-byte length field, detailing the number of bytes to follow in the frame

- The encrypted firmware data

- A 2-byte CRC

The maximum size of a frame depends on the type of device:

- For single maXTouch devices (such as the mXT224, mXT165 and mXT140) the maximum size of a frame is 276 bytes.

- For maXTouch chip set devices (such as the mXT1386 Chip Set) the maximum size of a frame is 532 bytes.

Figure 3-4. Data Frame Format

<img src="https://cdn.noedgeai.com/bo_d82amkqlb0pc73cnab4g_6.jpg?x=483&y=256&w=1029&h=176&r=0"/>

The application frames come from a file supplied by Atmel. This is one of two formats:

- A file containing only the frame data (see Figure 3-4)

- A file containing the frame data and header information (see Figure 3-5)

A file that contains header information starts with the characters ATML_VIH, and consists of a total of 22 characters. The header information will describe the contents of the encrypted firmware file and will be in the following format.

Figure 3-5. Encrypted Firmware Frame Format

<img src="https://cdn.noedgeai.com/bo_d82amkqlb0pc73cnab4g_6.jpg?x=436&y=818&w=1120&h=182&r=0"/>

The final frame within a firmware file contains an embedded reset command that causes the device to reset itself when it has finished updating the application. After resetting, the device performs a CRC on the application, asserts the CHG line and behaves according to incoming commands.

Alternatively, the host may cause the device to reset by sending a frame length word of zero.

If the device is reset part of the way through updating the application, the application will be corrupt and will need updating again.

Note: After updating the firmware the host should rewrite any stored settings used by the application.

## 4. BX：Bootloader 烧写 + 上位机上传固件（Host `.enc`）

本节在 QTAN0051 Level2 流程之上，补充**量产/维护场景**：器件已进入或可被切入 Bootloader 后，由**上位机（Host）**选择并上传加密固件文件，按帧写入完成应用更新。

### 4.1 目标固件文件

| 项目 | 说明 |
|------|------|
| 示例文件 | `ATMXT640UD_0x17_3.0.E3_PROD (1).enc`（与本说明同仓库时位于工程根目录；部署到产线工具时请放在可读路径） |
| 器件系列 | ATMXT640UD（具体 Bootloader ID / 帧长上限以器件协议与数据手册为准；单器件帧长上限参见 **§3.4**） |
| 文件形态 | 内容为**连续十六进制字符**的帧流（本文件**不以** `ATML_VIH` 开头），对应文档 **§3.4** 中「**仅含帧数据**」的一种分发格式。若文件以 `ATML_VIH` 开头，则先按 **Figure 3-5** 跳过 **22 字节** 头后再按帧解析。 |

文件名中的 `0x17`、`3.0.E3`、`PROD` 一般表示配置/版本/发布类型；**I²C 应用地址与 Bootloader 地址仍以硬件引脚与 §3.2 为准**，勿仅凭文件名改地址。

### 4.2 上位机侧流程（与 §2 / §3 对应）

1. **进入 Bootloader**（§2）：上电无固件/损坏自动进入；或在应用模式下通过 **§2.2～§2.4** 之一强制进入。
2. **确认 Bootloader**（§2.5）：在 **Bootloader 从地址**上读状态字节，**bit7、bit6 置位**表示 `WAITING_BOOTLOAD_CMD`（表 3-3）。
3. **解锁应用更新**（§3.1）：向该从地址写入 **`0xDC`、`0xAA`**。
4. **打开 Host 选择的 `.enc`**：读入文件 → 去除空白 → **十六进制解码为二进制缓冲区**（若为纯二进制 `.enc` 则跳过解码步骤）。
5. **按帧发送**（§3.4、图 3-4）：从缓冲区偏移 0 开始循环：
   - 读取 **2 字节帧长度字** \(L\)：表示长度字之后本帧还需连续读取的字节数（一般含加密载荷与 **2 字节 CRC**，与 Atmel 帧格式一致）。**Endian 以具体 `.enc` 分发为准**：本仓库示例 `ATMXT640UD_0x17_3.0.E3_PROD (1).enc` 为十六进制文本流、首帧以 `0012` 开头，即 **大端** \(L=0x0012=18\)。若大端/小端两种解读均落在允许帧长内，本仓库上位机 **`serial-app - 副本`** 中 `splitEncFrames` **优先大端**，与示例一致；其它分发若为小端，须与解析实现一致。
   - 若 \(L = 0\)，可按文档说明用于**主机触发复位**等语义（见 **§3.4** 末尾）。
   - 否则发送紧随其后的 **\(L\) 字节**整帧；**严格遵守 §3.2.2**：根据 **CHG** 与状态机在 `WAITING_FRAME_DATA` / CRC 各态之间握手，**不得在 CHG 未拉低时盲目轮询写总线**。
   - 若进入 `FRAME_CRC_FAIL`，在回到 `WAITING_FRAME_DATA` 后**重发上一帧**（表 3-3）。
6. **收尾**：最后一帧内嵌复位命令时，器件更新完成后自行复位；否则可由主机按文档发送 **长度为 0 的帧**等方式触发复位（§3.4）。
7. **应用侧配置**：固件更新后，Host 应**重新写入应用依赖的配置/校准等存储**（见文首 Introduction 与 §3.4 Note）。

### 4.3 功能要点小结

- **Bootloader 只负责接收协议规定的加密帧**；`.enc` 的解密/校验语义由器件内 Bootloader 完成，Host **不做固件解密**，只负责**可靠传输与状态机同步**。
- 上位机需实现：**文件选择（上传）**、**十六进制帧缓冲解析**、**I²C 写帧 + 按 CHG/状态读同步**，并与 **§3** 状态码一致。

### 4.4 与本仓库的对应关系（`test-V1.7` + `serial-app - 副本`）

| 组件 | 路径 / 说明 |
|------|-------------|
| 示例加密固件 | 工程根目录：`ATMXT640UD_0x17_3.0.E3_PROD (1).enc`（连续十六进制字符、无 `ATML_VIH` 头） |
| STM32 CDC → I²C 桥 | `test-V1.7/USB_DEVICE/App/usbd_cdc_if.c`：`FORCEBL`、桥模式、`CMD_READ_PINS`（CHG）、`0x53` Bootloader 大包写、`0xDC 0xAA` 解锁序列经桥转发至触摸芯片 |
| 上位机（Electron） | 目录 **`serial-app - 副本`**：`src/main/index.ts` 中 IPC **`flash-bootloader-enc`**；`src/preload/index.ts` 暴露 **`flashBootloaderEnc`** |
| 文件选择 | `select-config-file` 支持 `xcfg` / `bin` / `enc`；弹窗内选 `.enc` 后连接 **test-V1.7** CDC 再写入 |
| 可调参数（UI） | Bootloader **7-bit I²C**（默认 `0x25`；ADDR_SEL 低时常为 `0x24`）；是否**先发 `FORCEBL`**（已在 Bootloader 时可关） |

---

## Headquarters

## International

Atmel Asia Atmel Europe Atmel Japan

Unit 01-05 & 16, 19/F Le Krebs 9F, Tonetsu Shinkawa Bldg.

BEA Tower, Millennium City 5 8, Rue Jean-Pierre Timbaud 1-24-8 Shinkawa

Chuo-ku, Tokyo 104-0033 Japan

418 Kwun Tong Road BP 309

Kwun Tong 78054 Saint-Quentin-en-

Kowloon Yvelines Cedex Tel: (81) 3-3523-3551

Hong Kong France Fax: (81) 3-3523-7581

Tel: (852) 2245-6100 Tel: (33) 1-30-60-70-00

Fax: (852) 2722-1369 Fax: (33) 1-30-60-71-11

Touch Technology Division

1 Mitchell Point

Ensign Way

Hamble

Southampton

Hampshire SO31 4RF

United Kingdom

Tel: (44) 23-8056-5600

Fax: (44) 23-8045-3939

## Product Contact

Web Site Technical Support Sales Contact

www.atmel.com touch@atmel.com

Literature Requests

www.atmel.com/literature

Atmel Corporation

2325 Orchard Parkway

San Jose, CA 95131

USA

Tel: 1(408) 441-0311

Fax: 1(408) 487-2600

Disclaimer: The information in this document is provided in connection with Atmel products. No license, express or implied, by estoppel or otherwise, to any intellectual property right is granted by this document or in connection with the sale of Atmel products. EXCEPT AS SET FORTH IN ATMEL’S TERMS AND CONDITIONS OF SALE LOCATED ON ATMEL'S WEB SITE, ATMEL ASSUMES NO LIABILITY WHATSOEVER AND DISCLAIMS ANY EXPRESS, IMPLIED OR STATUTORY Warranty relating to its products including, but not limited to, the implied warranty of merchantability, fitness for a particle. ular purpose, or non-infringement. In no event shall atmel be liable for any direct, indirect, ionsequential, punitive, special OR INCIDENTAL DAMAGES (INCLUDING, WITHOUT LIMITATION, DAMAGES FOR LOSS OF PROFITS, BUSINESS INTERRUPTION, OR LOSS OF INFORMATION) ARISING OUT OF THE USE OR INABILITY TO USE THIS DOCUMENT, EVEN IF ATMEL HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGES. Atmel makes no representations or warranties with respect to the accuracy or completeness of the contents of this document and reserves the right to make changes to specifications and product descriptions at any time without notice. Aimel does not make any commitment to update their information contained herein. Unless specifically provided otherwise. Atmel products are not suitable for, and shall not be used in, automotive applications. Atmel's products are not intended, authorized, or warranted for use as components in applications intended to support or sustain life.

© 2009 - 2010 Atmel Corporation. All rights reserved. Atmel®, Atmel logo and combinations thereof, QMatrix® and others are registered trademarks,maXTouch \( {}^{7!!} \) and others are trademarks of Atmel Corporation or its subsidiaries. Other terms and product names may be registered trademarks or trademarks of others.

