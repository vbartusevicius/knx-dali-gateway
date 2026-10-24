#pragma once


#define paramDelay(time) (uint32_t)( \
            (time & 0xC000) == 0xC000 ? (time & 0x3FFF) * 100 : \
            (time & 0xC000) == 0x0000 ? (time & 0x3FFF) * 1000 : \
            (time & 0xC000) == 0x4000 ? (time & 0x3FFF) * 60000 : \
            (time & 0xC000) == 0x8000 ? ((time & 0x3FFF) > 1000 ? 3600000 : \
                                         (time & 0x3FFF) * 3600000 ) : 0 )
                                             
#define MAIN_FirmwareName "KNX DALI Gateway"
#define MAIN_OpenKnxId 0xFF
#define MAIN_ApplicationNumber 90
#define MAIN_ApplicationVersion 3
#define MAIN_ApplicationEncoding iso-8859-15
#define MAIN_ParameterSize 909
#define MAIN_MaxKoNumber 1321
#define MAIN_OrderNumber "0xFF5A"
// Parameter with single occurrence


#define CH_StartupDelay                         0      // uint8_t
#define CH_BcSwitchOnMode                       1      // 8 Bits, Bit 7-0
#define CH_BcFixedLevel                         2      // uint8_t
#define CH_DaliChannel                          3      // 8 Bits, Bit 7-0
#define CH_InternalDaliPsu                      4      // 8 Bits, Bit 7-0

// Startup delay
#define ParamCH_StartupDelay                        (knx.paramByte(CH_StartupDelay))
// Switch-on level
#define ParamCH_BcSwitchOnMode                      (knx.paramByte(CH_BcSwitchOnMode))
// Fixed switch-on level
#define ParamCH_BcFixedLevel                        (knx.paramByte(CH_BcFixedLevel))
// Lamp
#define ParamCH_DaliChannel                         (knx.paramByte(CH_DaliChannel))
// Internal DALI bus supply (15 V, 180 mA)
#define ParamCH_InternalDaliPsu                     (knx.paramByte(CH_InternalDaliPsu))

#define CH_KoBcSwitch 1185
#define CH_KoBcBrightness 1186
#define CH_KoBcRelativeDimming 1187
#define CH_KoBcScene 1188
#define CH_KoBcColourTemperature 1189
#define CH_KoBcRelativeColourTemperature 1190
#define CH_KoBcRgb 1191
#define CH_KoBcRgbw 1192
#define CH_KoBcXyY 1193

// Broadcast
#define KoCH_BcSwitch                            (knx.getGroupObject(CH_KoBcSwitch))
// Broadcast
#define KoCH_BcBrightness                        (knx.getGroupObject(CH_KoBcBrightness))
// Broadcast
#define KoCH_BcRelativeDimming                   (knx.getGroupObject(CH_KoBcRelativeDimming))
// Broadcast
#define KoCH_BcScene                             (knx.getGroupObject(CH_KoBcScene))
// Broadcast
#define KoCH_BcColourTemperature                 (knx.getGroupObject(CH_KoBcColourTemperature))
// Broadcast
#define KoCH_BcRelativeColourTemperature         (knx.getGroupObject(CH_KoBcRelativeColourTemperature))
// Broadcast
#define KoCH_BcRgb                               (knx.getGroupObject(CH_KoBcRgb))
// Broadcast
#define KoCH_BcRgbw                              (knx.getGroupObject(CH_KoBcRgbw))
// Broadcast
#define KoCH_BcXyY                               (knx.getGroupObject(CH_KoBcXyY))

#define CH_ChannelCount 64

// Parameter per channel
#define CH_ParamBlockOffset 5
#define CH_ParamBlockSize 11
#define CH_ParamCalcIndex(index) (index + CH_ParamBlockOffset + _channelIndex * CH_ParamBlockSize)

#define CH_fSwitchOnMode                        0      // 8 Bits, Bit 7-0
#define CH_fFixedLevel                          1      // uint8_t
#define CH_fGroup01                             2      // 1 Bit, Bit 0
#define     CH_fGroup01Mask 0x01
#define     CH_fGroup01Shift 0
#define CH_fGroup02                             2      // 1 Bit, Bit 1
#define     CH_fGroup02Mask 0x02
#define     CH_fGroup02Shift 1
#define CH_fGroup03                             2      // 1 Bit, Bit 2
#define     CH_fGroup03Mask 0x04
#define     CH_fGroup03Shift 2
#define CH_fGroup04                             2      // 1 Bit, Bit 3
#define     CH_fGroup04Mask 0x08
#define     CH_fGroup04Shift 3
#define CH_fGroup05                             2      // 1 Bit, Bit 4
#define     CH_fGroup05Mask 0x10
#define     CH_fGroup05Shift 4
#define CH_fGroup06                             2      // 1 Bit, Bit 5
#define     CH_fGroup06Mask 0x20
#define     CH_fGroup06Shift 5
#define CH_fGroup07                             2      // 1 Bit, Bit 6
#define     CH_fGroup07Mask 0x40
#define     CH_fGroup07Shift 6
#define CH_fGroup08                             2      // 1 Bit, Bit 7
#define     CH_fGroup08Mask 0x80
#define     CH_fGroup08Shift 7
#define CH_fGroup09                             3      // 1 Bit, Bit 0
#define     CH_fGroup09Mask 0x01
#define     CH_fGroup09Shift 0
#define CH_fGroup10                             3      // 1 Bit, Bit 1
#define     CH_fGroup10Mask 0x02
#define     CH_fGroup10Shift 1
#define CH_fGroup11                             3      // 1 Bit, Bit 2
#define     CH_fGroup11Mask 0x04
#define     CH_fGroup11Shift 2
#define CH_fGroup12                             3      // 1 Bit, Bit 3
#define     CH_fGroup12Mask 0x08
#define     CH_fGroup12Shift 3
#define CH_fGroup13                             3      // 1 Bit, Bit 4
#define     CH_fGroup13Mask 0x10
#define     CH_fGroup13Shift 4
#define CH_fGroup14                             3      // 1 Bit, Bit 5
#define     CH_fGroup14Mask 0x20
#define     CH_fGroup14Shift 5
#define CH_fGroup15                             3      // 1 Bit, Bit 6
#define     CH_fGroup15Mask 0x40
#define     CH_fGroup15Shift 6
#define CH_fGroup16                             3      // 1 Bit, Bit 7
#define     CH_fGroup16Mask 0x80
#define     CH_fGroup16Shift 7
#define CH_fColourControl                       4      // 8 Bits, Bit 7-0
#define CH_fMinLevel                            5      // uint8_t
#define CH_fMaxLevel                            6      // uint8_t
#define CH_fPowerOnLevel                        7      // uint8_t
#define CH_fSysFailLevel                        8      // uint8_t
#define CH_fFadeTime                            9      // 8 Bits, Bit 7-0
#define CH_fFadeRate                           10      // 8 Bits, Bit 7-0

// Switch-on level
#define ParamCH_fSwitchOnMode                       (knx.paramByte(CH_ParamCalcIndex(CH_fSwitchOnMode)))
// Fixed switch-on level
#define ParamCH_fFixedLevel                         (knx.paramByte(CH_ParamCalcIndex(CH_fFixedLevel)))
// Member of G00
#define ParamCH_fGroup01                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup01)) & CH_fGroup01Mask))
// Member of G01
#define ParamCH_fGroup02                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup02)) & CH_fGroup02Mask))
// Member of G02
#define ParamCH_fGroup03                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup03)) & CH_fGroup03Mask))
// Member of G03
#define ParamCH_fGroup04                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup04)) & CH_fGroup04Mask))
// Member of G04
#define ParamCH_fGroup05                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup05)) & CH_fGroup05Mask))
// Member of G05
#define ParamCH_fGroup06                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup06)) & CH_fGroup06Mask))
// Member of G06
#define ParamCH_fGroup07                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup07)) & CH_fGroup07Mask))
// Member of G07
#define ParamCH_fGroup08                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup08)) & CH_fGroup08Mask))
// Member of G08
#define ParamCH_fGroup09                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup09)) & CH_fGroup09Mask))
// Member of G09
#define ParamCH_fGroup10                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup10)) & CH_fGroup10Mask))
// Member of G10
#define ParamCH_fGroup11                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup11)) & CH_fGroup11Mask))
// Member of G11
#define ParamCH_fGroup12                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup12)) & CH_fGroup12Mask))
// Member of G12
#define ParamCH_fGroup13                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup13)) & CH_fGroup13Mask))
// Member of G13
#define ParamCH_fGroup14                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup14)) & CH_fGroup14Mask))
// Member of G14
#define ParamCH_fGroup15                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup15)) & CH_fGroup15Mask))
// Member of G15
#define ParamCH_fGroup16                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup16)) & CH_fGroup16Mask))
// Lamp type
#define ParamCH_fColourControl                      (knx.paramByte(CH_ParamCalcIndex(CH_fColourControl)))
// Minimum level
#define ParamCH_fMinLevel                           (knx.paramByte(CH_ParamCalcIndex(CH_fMinLevel)))
// Maximum level
#define ParamCH_fMaxLevel                           (knx.paramByte(CH_ParamCalcIndex(CH_fMaxLevel)))
// Power-on level (0 = off, 101 = last level)
#define ParamCH_fPowerOnLevel                       (knx.paramByte(CH_ParamCalcIndex(CH_fPowerOnLevel)))
// System-failure level (0 = off, 101 = no change)
#define ParamCH_fSysFailLevel                       (knx.paramByte(CH_ParamCalcIndex(CH_fSysFailLevel)))
// Fade time
#define ParamCH_fFadeTime                           (knx.paramByte(CH_ParamCalcIndex(CH_fFadeTime)))
// Fade rate
#define ParamCH_fFadeRate                           (knx.paramByte(CH_ParamCalcIndex(CH_fFadeRate)))

// deprecated
#define CH_KoOffset 1

// Communication objects per channel (multiple occurrence)
#define CH_KoBlockOffset 1
#define CH_KoBlockSize 15

#define CH_KoCalcNumber(index) (index + CH_KoBlockOffset + _channelIndex * CH_KoBlockSize)
#define CH_KoCalcIndex(number) ((number >= CH_KoCalcNumber(0) && number < CH_KoCalcNumber(CH_KoBlockSize)) ? (number - CH_KoBlockOffset) % CH_KoBlockSize : -1)
#define CH_KoCalcChannel(number) ((number >= CH_KoBlockOffset && number < CH_KoBlockOffset + CH_ChannelCount * CH_KoBlockSize) ? (number - CH_KoBlockOffset) / CH_KoBlockSize : -1)

#define CH_KoSwitch 0
#define CH_KoSwitchStatus 1
#define CH_KoBrightness 2
#define CH_KoBrightnessStatus 3
#define CH_KoRelativeDimming 4
#define CH_KoFault 5
#define CH_KoColourTemperature 6
#define CH_KoColourTemperatureStatus 7
#define CH_KoRelativeColourTemperature 8
#define CH_KoRgb 9
#define CH_KoRgbStatus 10
#define CH_KoRgbw 11
#define CH_KoRgbwStatus 12
#define CH_KoXyY 13
#define CH_KoXyYStatus 14

// A%CC-1%
#define KoCH_Switch                              (knx.getGroupObject(CH_KoCalcNumber(CH_KoSwitch)))
// A%CC-1%
#define KoCH_SwitchStatus                        (knx.getGroupObject(CH_KoCalcNumber(CH_KoSwitchStatus)))
// A%CC-1%
#define KoCH_Brightness                          (knx.getGroupObject(CH_KoCalcNumber(CH_KoBrightness)))
// A%CC-1%
#define KoCH_BrightnessStatus                    (knx.getGroupObject(CH_KoCalcNumber(CH_KoBrightnessStatus)))
// A%CC-1%
#define KoCH_RelativeDimming                     (knx.getGroupObject(CH_KoCalcNumber(CH_KoRelativeDimming)))
// A%CC-1%
#define KoCH_Fault                               (knx.getGroupObject(CH_KoCalcNumber(CH_KoFault)))
// A%CC-1%
#define KoCH_ColourTemperature                   (knx.getGroupObject(CH_KoCalcNumber(CH_KoColourTemperature)))
// A%CC-1%
#define KoCH_ColourTemperatureStatus             (knx.getGroupObject(CH_KoCalcNumber(CH_KoColourTemperatureStatus)))
// A%CC-1%
#define KoCH_RelativeColourTemperature           (knx.getGroupObject(CH_KoCalcNumber(CH_KoRelativeColourTemperature)))
// A%CC-1%
#define KoCH_Rgb                                 (knx.getGroupObject(CH_KoCalcNumber(CH_KoRgb)))
// A%CC-1%
#define KoCH_RgbStatus                           (knx.getGroupObject(CH_KoCalcNumber(CH_KoRgbStatus)))
// A%CC-1%
#define KoCH_Rgbw                                (knx.getGroupObject(CH_KoCalcNumber(CH_KoRgbw)))
// A%CC-1%
#define KoCH_RgbwStatus                          (knx.getGroupObject(CH_KoCalcNumber(CH_KoRgbwStatus)))
// A%CC-1%
#define KoCH_XyY                                 (knx.getGroupObject(CH_KoCalcNumber(CH_KoXyY)))
// A%CC-1%
#define KoCH_XyYStatus                           (knx.getGroupObject(CH_KoCalcNumber(CH_KoXyYStatus)))

#define GR_ChannelCount 16

// Parameter per channel
#define GR_ParamBlockOffset 709
#define GR_ParamBlockSize 3
#define GR_ParamCalcIndex(index) (index + GR_ParamBlockOffset + _channelIndex * GR_ParamBlockSize)

#define GR_fSwitchOnMode                        0      // 8 Bits, Bit 7-0
#define GR_fFixedLevel                          1      // uint8_t
#define GR_fColourControl                       2      // 8 Bits, Bit 7-0

// Switch-on level
#define ParamGR_fSwitchOnMode                       (knx.paramByte(GR_ParamCalcIndex(GR_fSwitchOnMode)))
// Fixed switch-on level
#define ParamGR_fFixedLevel                         (knx.paramByte(GR_ParamCalcIndex(GR_fFixedLevel)))
// Group type
#define ParamGR_fColourControl                      (knx.paramByte(GR_ParamCalcIndex(GR_fColourControl)))

// deprecated
#define GR_KoOffset 961

// Communication objects per channel (multiple occurrence)
#define GR_KoBlockOffset 961
#define GR_KoBlockSize 14

#define GR_KoCalcNumber(index) (index + GR_KoBlockOffset + _channelIndex * GR_KoBlockSize)
#define GR_KoCalcIndex(number) ((number >= GR_KoCalcNumber(0) && number < GR_KoCalcNumber(GR_KoBlockSize)) ? (number - GR_KoBlockOffset) % GR_KoBlockSize : -1)
#define GR_KoCalcChannel(number) ((number >= GR_KoBlockOffset && number < GR_KoBlockOffset + GR_ChannelCount * GR_KoBlockSize) ? (number - GR_KoBlockOffset) / GR_KoBlockSize : -1)

#define GR_KoSwitch 0
#define GR_KoSwitchStatus 1
#define GR_KoBrightness 2
#define GR_KoBrightnessStatus 3
#define GR_KoRelativeDimming 4
#define GR_KoColourTemperature 5
#define GR_KoColourTemperatureStatus 6
#define GR_KoRelativeColourTemperature 7
#define GR_KoRgb 8
#define GR_KoRgbStatus 9
#define GR_KoRgbw 10
#define GR_KoRgbwStatus 11
#define GR_KoXyY 12
#define GR_KoXyYStatus 13

// G%CC-1%
#define KoGR_Switch                              (knx.getGroupObject(GR_KoCalcNumber(GR_KoSwitch)))
// G%CC-1%
#define KoGR_SwitchStatus                        (knx.getGroupObject(GR_KoCalcNumber(GR_KoSwitchStatus)))
// G%CC-1%
#define KoGR_Brightness                          (knx.getGroupObject(GR_KoCalcNumber(GR_KoBrightness)))
// G%CC-1%
#define KoGR_BrightnessStatus                    (knx.getGroupObject(GR_KoCalcNumber(GR_KoBrightnessStatus)))
// G%CC-1%
#define KoGR_RelativeDimming                     (knx.getGroupObject(GR_KoCalcNumber(GR_KoRelativeDimming)))
// G%CC-1%
#define KoGR_ColourTemperature                   (knx.getGroupObject(GR_KoCalcNumber(GR_KoColourTemperature)))
// G%CC-1%
#define KoGR_ColourTemperatureStatus             (knx.getGroupObject(GR_KoCalcNumber(GR_KoColourTemperatureStatus)))
// G%CC-1%
#define KoGR_RelativeColourTemperature           (knx.getGroupObject(GR_KoCalcNumber(GR_KoRelativeColourTemperature)))
// G%CC-1%
#define KoGR_Rgb                                 (knx.getGroupObject(GR_KoCalcNumber(GR_KoRgb)))
// G%CC-1%
#define KoGR_RgbStatus                           (knx.getGroupObject(GR_KoCalcNumber(GR_KoRgbStatus)))
// G%CC-1%
#define KoGR_Rgbw                                (knx.getGroupObject(GR_KoCalcNumber(GR_KoRgbw)))
// G%CC-1%
#define KoGR_RgbwStatus                          (knx.getGroupObject(GR_KoCalcNumber(GR_KoRgbwStatus)))
// G%CC-1%
#define KoGR_XyY                                 (knx.getGroupObject(GR_KoCalcNumber(GR_KoXyY)))
// G%CC-1%
#define KoGR_XyYStatus                           (knx.getGroupObject(GR_KoCalcNumber(GR_KoXyYStatus)))

#define IN_ChannelCount 8

// Parameter per channel
#define IN_ParamBlockOffset 757
#define IN_ParamBlockSize 19
#define IN_ParamCalcIndex(index) (index + IN_ParamBlockOffset + _channelIndex * IN_ParamBlockSize)

#define IN_fDeviceAddress                       0      // 8 Bits, Bit 7-0
#define IN_fFirstInstance                       1      // uint8_t
#define IN_fInstanceCount                       2      // 8 Bits, Bit 7-0
#define IN_fI0Type                              3      // 8 Bits, Bit 7-0
#define IN_fI0Function                          4      // 8 Bits, Bit 7-0
#define IN_fI0Scene                             5      // uint8_t
#define IN_fI0Threshold                         6      // uint8_t
#define IN_fI1Type                              7      // 8 Bits, Bit 7-0
#define IN_fI1Function                          8      // 8 Bits, Bit 7-0
#define IN_fI1Scene                             9      // uint8_t
#define IN_fI1Threshold                        10      // uint8_t
#define IN_fI2Type                             11      // 8 Bits, Bit 7-0
#define IN_fI2Function                         12      // 8 Bits, Bit 7-0
#define IN_fI2Scene                            13      // uint8_t
#define IN_fI2Threshold                        14      // uint8_t
#define IN_fI3Type                             15      // 8 Bits, Bit 7-0
#define IN_fI3Function                         16      // 8 Bits, Bit 7-0
#define IN_fI3Scene                            17      // uint8_t
#define IN_fI3Threshold                        18      // uint8_t

// Input device
#define ParamIN_fDeviceAddress                      (knx.paramByte(IN_ParamCalcIndex(IN_fDeviceAddress)))
// First instance
#define ParamIN_fFirstInstance                      (knx.paramByte(IN_ParamCalcIndex(IN_fFirstInstance)))
// Instances
#define ParamIN_fInstanceCount                      (knx.paramByte(IN_ParamCalcIndex(IN_fInstanceCount)))
// Type
#define ParamIN_fI0Type                             (knx.paramByte(IN_ParamCalcIndex(IN_fI0Type)))
// Push button function
#define ParamIN_fI0Function                         (knx.paramByte(IN_ParamCalcIndex(IN_fI0Function)))
// Scene number
#define ParamIN_fI0Scene                            (knx.paramByte(IN_ParamCalcIndex(IN_fI0Scene)))
// Send on change of (0 = every event)
#define ParamIN_fI0Threshold                        (knx.paramByte(IN_ParamCalcIndex(IN_fI0Threshold)))
// Type
#define ParamIN_fI1Type                             (knx.paramByte(IN_ParamCalcIndex(IN_fI1Type)))
// Push button function
#define ParamIN_fI1Function                         (knx.paramByte(IN_ParamCalcIndex(IN_fI1Function)))
// Scene number
#define ParamIN_fI1Scene                            (knx.paramByte(IN_ParamCalcIndex(IN_fI1Scene)))
// Send on change of (0 = every event)
#define ParamIN_fI1Threshold                        (knx.paramByte(IN_ParamCalcIndex(IN_fI1Threshold)))
// Type
#define ParamIN_fI2Type                             (knx.paramByte(IN_ParamCalcIndex(IN_fI2Type)))
// Push button function
#define ParamIN_fI2Function                         (knx.paramByte(IN_ParamCalcIndex(IN_fI2Function)))
// Scene number
#define ParamIN_fI2Scene                            (knx.paramByte(IN_ParamCalcIndex(IN_fI2Scene)))
// Send on change of (0 = every event)
#define ParamIN_fI2Threshold                        (knx.paramByte(IN_ParamCalcIndex(IN_fI2Threshold)))
// Type
#define ParamIN_fI3Type                             (knx.paramByte(IN_ParamCalcIndex(IN_fI3Type)))
// Push button function
#define ParamIN_fI3Function                         (knx.paramByte(IN_ParamCalcIndex(IN_fI3Function)))
// Scene number
#define ParamIN_fI3Scene                            (knx.paramByte(IN_ParamCalcIndex(IN_fI3Scene)))
// Send on change of (0 = every event)
#define ParamIN_fI3Threshold                        (knx.paramByte(IN_ParamCalcIndex(IN_fI3Threshold)))

// deprecated
#define IN_KoOffset 1194

// Communication objects per channel (multiple occurrence)
#define IN_KoBlockOffset 1194
#define IN_KoBlockSize 16

#define IN_KoCalcNumber(index) (index + IN_KoBlockOffset + _channelIndex * IN_KoBlockSize)
#define IN_KoCalcIndex(number) ((number >= IN_KoCalcNumber(0) && number < IN_KoCalcNumber(IN_KoBlockSize)) ? (number - IN_KoBlockOffset) % IN_KoBlockSize : -1)
#define IN_KoCalcChannel(number) ((number >= IN_KoBlockOffset && number < IN_KoBlockOffset + IN_ChannelCount * IN_KoBlockSize) ? (number - IN_KoBlockOffset) / IN_KoBlockSize : -1)

#define IN_KoI0Switch 0
#define IN_KoI0Dimming 1
#define IN_KoI0Illuminance 2
#define IN_KoI0Scene 3
#define IN_KoI1Switch 4
#define IN_KoI1Dimming 5
#define IN_KoI1Illuminance 6
#define IN_KoI1Scene 7
#define IN_KoI2Switch 8
#define IN_KoI2Dimming 9
#define IN_KoI2Illuminance 10
#define IN_KoI2Scene 11
#define IN_KoI3Switch 12
#define IN_KoI3Dimming 13
#define IN_KoI3Illuminance 14
#define IN_KoI3Scene 15

// Input device %C%
#define KoIN_I0Switch                            (knx.getGroupObject(IN_KoCalcNumber(IN_KoI0Switch)))
// Input device %C%
#define KoIN_I0Dimming                           (knx.getGroupObject(IN_KoCalcNumber(IN_KoI0Dimming)))
// Input device %C%
#define KoIN_I0Illuminance                       (knx.getGroupObject(IN_KoCalcNumber(IN_KoI0Illuminance)))
// Input device %C%
#define KoIN_I0Scene                             (knx.getGroupObject(IN_KoCalcNumber(IN_KoI0Scene)))
// Input device %C%
#define KoIN_I1Switch                            (knx.getGroupObject(IN_KoCalcNumber(IN_KoI1Switch)))
// Input device %C%
#define KoIN_I1Dimming                           (knx.getGroupObject(IN_KoCalcNumber(IN_KoI1Dimming)))
// Input device %C%
#define KoIN_I1Illuminance                       (knx.getGroupObject(IN_KoCalcNumber(IN_KoI1Illuminance)))
// Input device %C%
#define KoIN_I1Scene                             (knx.getGroupObject(IN_KoCalcNumber(IN_KoI1Scene)))
// Input device %C%
#define KoIN_I2Switch                            (knx.getGroupObject(IN_KoCalcNumber(IN_KoI2Switch)))
// Input device %C%
#define KoIN_I2Dimming                           (knx.getGroupObject(IN_KoCalcNumber(IN_KoI2Dimming)))
// Input device %C%
#define KoIN_I2Illuminance                       (knx.getGroupObject(IN_KoCalcNumber(IN_KoI2Illuminance)))
// Input device %C%
#define KoIN_I2Scene                             (knx.getGroupObject(IN_KoCalcNumber(IN_KoI2Scene)))
// Input device %C%
#define KoIN_I3Switch                            (knx.getGroupObject(IN_KoCalcNumber(IN_KoI3Switch)))
// Input device %C%
#define KoIN_I3Dimming                           (knx.getGroupObject(IN_KoCalcNumber(IN_KoI3Dimming)))
// Input device %C%
#define KoIN_I3Illuminance                       (knx.getGroupObject(IN_KoCalcNumber(IN_KoI3Illuminance)))
// Input device %C%
#define KoIN_I3Scene                             (knx.getGroupObject(IN_KoCalcNumber(IN_KoI3Scene)))


// enumeration types


#ifdef MAIN_FirmwareRevision
#ifndef FIRMWARE_REVISION
#define FIRMWARE_REVISION MAIN_FirmwareRevision
#endif
#endif
#ifdef MAIN_FirmwareName
#ifndef FIRMWARE_NAME
#define FIRMWARE_NAME MAIN_FirmwareName
#endif
#endif
