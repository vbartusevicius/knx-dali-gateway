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
#define MAIN_ApplicationVersion 1
#define MAIN_ApplicationEncoding iso-8859-15
#define MAIN_ParameterSize 564
#define MAIN_MaxKoNumber 1321
#define MAIN_OrderNumber "0xFF5A"
// Parameter with single occurrence


#define CH_StartupDelay                         0      // uint8_t
#define CH_BcSwitchOnMode                       1      // 8 Bits, Bit 7-0
#define CH_BcFixedLevel                         2      // uint8_t
#define CH_DaliChannel                          3      // uint8_t

// Startup delay
#define ParamCH_StartupDelay                        (knx.paramByte(CH_StartupDelay))
// Broadcast switch-on level
#define ParamCH_BcSwitchOnMode                      (knx.paramByte(CH_BcSwitchOnMode))
// Broadcast fixed switch-on level
#define ParamCH_BcFixedLevel                        (knx.paramByte(CH_BcFixedLevel))
// DALI channel
#define ParamCH_DaliChannel                         (knx.paramByte(CH_DaliChannel))

#define CH_KoBcSwitch 1185
#define CH_KoBcBrightness 1186
#define CH_KoBcRelativeDimming 1187
#define CH_KoBcScene 1188
#define CH_KoBcColourTemperature 1189
#define CH_KoBcRelativeColourTemperature 1190
#define CH_KoBcRgb 1191
#define CH_KoBcRgbw 1192
#define CH_KoBcXyY 1193

// Broadcast: Switch
#define KoCH_BcSwitch                            (knx.getGroupObject(CH_KoBcSwitch))
// Broadcast: Brightness
#define KoCH_BcBrightness                        (knx.getGroupObject(CH_KoBcBrightness))
// Broadcast: Relative dimming
#define KoCH_BcRelativeDimming                   (knx.getGroupObject(CH_KoBcRelativeDimming))
// Broadcast: Scene
#define KoCH_BcScene                             (knx.getGroupObject(CH_KoBcScene))
// Broadcast: Colour temperature
#define KoCH_BcColourTemperature                 (knx.getGroupObject(CH_KoBcColourTemperature))
// Broadcast: Relative colour temperature
#define KoCH_BcRelativeColourTemperature         (knx.getGroupObject(CH_KoBcRelativeColourTemperature))
// Broadcast: RGB
#define KoCH_BcRgb                               (knx.getGroupObject(CH_KoBcRgb))
// Broadcast: RGBW
#define KoCH_BcRgbw                              (knx.getGroupObject(CH_KoBcRgbw))
// Broadcast: xyY
#define KoCH_BcXyY                               (knx.getGroupObject(CH_KoBcXyY))

#define CH_ChannelCount 64

// Parameter per channel
#define CH_ParamBlockOffset 4
#define CH_ParamBlockSize 5
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

// Switch-on level
#define ParamCH_fSwitchOnMode                       (knx.paramByte(CH_ParamCalcIndex(CH_fSwitchOnMode)))
// Fixed switch-on level
#define ParamCH_fFixedLevel                         (knx.paramByte(CH_ParamCalcIndex(CH_fFixedLevel)))
// Member of DALI group 1
#define ParamCH_fGroup01                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup01)) & CH_fGroup01Mask))
// Member of DALI group 2
#define ParamCH_fGroup02                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup02)) & CH_fGroup02Mask))
// Member of DALI group 3
#define ParamCH_fGroup03                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup03)) & CH_fGroup03Mask))
// Member of DALI group 4
#define ParamCH_fGroup04                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup04)) & CH_fGroup04Mask))
// Member of DALI group 5
#define ParamCH_fGroup05                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup05)) & CH_fGroup05Mask))
// Member of DALI group 6
#define ParamCH_fGroup06                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup06)) & CH_fGroup06Mask))
// Member of DALI group 7
#define ParamCH_fGroup07                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup07)) & CH_fGroup07Mask))
// Member of DALI group 8
#define ParamCH_fGroup08                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup08)) & CH_fGroup08Mask))
// Member of DALI group 9
#define ParamCH_fGroup09                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup09)) & CH_fGroup09Mask))
// Member of DALI group 10
#define ParamCH_fGroup10                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup10)) & CH_fGroup10Mask))
// Member of DALI group 11
#define ParamCH_fGroup11                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup11)) & CH_fGroup11Mask))
// Member of DALI group 12
#define ParamCH_fGroup12                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup12)) & CH_fGroup12Mask))
// Member of DALI group 13
#define ParamCH_fGroup13                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup13)) & CH_fGroup13Mask))
// Member of DALI group 14
#define ParamCH_fGroup14                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup14)) & CH_fGroup14Mask))
// Member of DALI group 15
#define ParamCH_fGroup15                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup15)) & CH_fGroup15Mask))
// Member of DALI group 16
#define ParamCH_fGroup16                            ((bool)(knx.paramByte(CH_ParamCalcIndex(CH_fGroup16)) & CH_fGroup16Mask))
// Colour control
#define ParamCH_fColourControl                      (knx.paramByte(CH_ParamCalcIndex(CH_fColourControl)))

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

// Channel %C%: Switch
#define KoCH_Switch                              (knx.getGroupObject(CH_KoCalcNumber(CH_KoSwitch)))
// Channel %C%: Switch status
#define KoCH_SwitchStatus                        (knx.getGroupObject(CH_KoCalcNumber(CH_KoSwitchStatus)))
// Channel %C%: Brightness
#define KoCH_Brightness                          (knx.getGroupObject(CH_KoCalcNumber(CH_KoBrightness)))
// Channel %C%: Brightness status
#define KoCH_BrightnessStatus                    (knx.getGroupObject(CH_KoCalcNumber(CH_KoBrightnessStatus)))
// Channel %C%: Relative dimming
#define KoCH_RelativeDimming                     (knx.getGroupObject(CH_KoCalcNumber(CH_KoRelativeDimming)))
// Channel %C%: Fault
#define KoCH_Fault                               (knx.getGroupObject(CH_KoCalcNumber(CH_KoFault)))
// Channel %C%: Colour temperature
#define KoCH_ColourTemperature                   (knx.getGroupObject(CH_KoCalcNumber(CH_KoColourTemperature)))
// Channel %C%: Colour temperature status
#define KoCH_ColourTemperatureStatus             (knx.getGroupObject(CH_KoCalcNumber(CH_KoColourTemperatureStatus)))
// Channel %C%: Relative colour temperature
#define KoCH_RelativeColourTemperature           (knx.getGroupObject(CH_KoCalcNumber(CH_KoRelativeColourTemperature)))
// Channel %C%: RGB
#define KoCH_Rgb                                 (knx.getGroupObject(CH_KoCalcNumber(CH_KoRgb)))
// Channel %C%: RGB status
#define KoCH_RgbStatus                           (knx.getGroupObject(CH_KoCalcNumber(CH_KoRgbStatus)))
// Channel %C%: RGBW
#define KoCH_Rgbw                                (knx.getGroupObject(CH_KoCalcNumber(CH_KoRgbw)))
// Channel %C%: RGBW status
#define KoCH_RgbwStatus                          (knx.getGroupObject(CH_KoCalcNumber(CH_KoRgbwStatus)))
// Channel %C%: xyY
#define KoCH_XyY                                 (knx.getGroupObject(CH_KoCalcNumber(CH_KoXyY)))
// Channel %C%: xyY status
#define KoCH_XyYStatus                           (knx.getGroupObject(CH_KoCalcNumber(CH_KoXyYStatus)))

#define GR_ChannelCount 16

// Parameter per channel
#define GR_ParamBlockOffset 324
#define GR_ParamBlockSize 3
#define GR_ParamCalcIndex(index) (index + GR_ParamBlockOffset + _channelIndex * GR_ParamBlockSize)

#define GR_fSwitchOnMode                        0      // 8 Bits, Bit 7-0
#define GR_fFixedLevel                          1      // uint8_t
#define GR_fColourControl                       2      // 8 Bits, Bit 7-0

// Switch-on level
#define ParamGR_fSwitchOnMode                       (knx.paramByte(GR_ParamCalcIndex(GR_fSwitchOnMode)))
// Fixed switch-on level
#define ParamGR_fFixedLevel                         (knx.paramByte(GR_ParamCalcIndex(GR_fFixedLevel)))
// Colour control
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

// DALI group %C%: Switch
#define KoGR_Switch                              (knx.getGroupObject(GR_KoCalcNumber(GR_KoSwitch)))
// DALI group %C%: Switch status
#define KoGR_SwitchStatus                        (knx.getGroupObject(GR_KoCalcNumber(GR_KoSwitchStatus)))
// DALI group %C%: Brightness
#define KoGR_Brightness                          (knx.getGroupObject(GR_KoCalcNumber(GR_KoBrightness)))
// DALI group %C%: Brightness status
#define KoGR_BrightnessStatus                    (knx.getGroupObject(GR_KoCalcNumber(GR_KoBrightnessStatus)))
// DALI group %C%: Relative dimming
#define KoGR_RelativeDimming                     (knx.getGroupObject(GR_KoCalcNumber(GR_KoRelativeDimming)))
// DALI group %C%: Colour temperature
#define KoGR_ColourTemperature                   (knx.getGroupObject(GR_KoCalcNumber(GR_KoColourTemperature)))
// DALI group %C%: Colour temperature status
#define KoGR_ColourTemperatureStatus             (knx.getGroupObject(GR_KoCalcNumber(GR_KoColourTemperatureStatus)))
// DALI group %C%: Relative colour temperature
#define KoGR_RelativeColourTemperature           (knx.getGroupObject(GR_KoCalcNumber(GR_KoRelativeColourTemperature)))
// DALI group %C%: RGB
#define KoGR_Rgb                                 (knx.getGroupObject(GR_KoCalcNumber(GR_KoRgb)))
// DALI group %C%: RGB status
#define KoGR_RgbStatus                           (knx.getGroupObject(GR_KoCalcNumber(GR_KoRgbStatus)))
// DALI group %C%: RGBW
#define KoGR_Rgbw                                (knx.getGroupObject(GR_KoCalcNumber(GR_KoRgbw)))
// DALI group %C%: RGBW status
#define KoGR_RgbwStatus                          (knx.getGroupObject(GR_KoCalcNumber(GR_KoRgbwStatus)))
// DALI group %C%: xyY
#define KoGR_XyY                                 (knx.getGroupObject(GR_KoCalcNumber(GR_KoXyY)))
// DALI group %C%: xyY status
#define KoGR_XyYStatus                           (knx.getGroupObject(GR_KoCalcNumber(GR_KoXyYStatus)))

#define IN_ChannelCount 32

// Parameter per channel
#define IN_ParamBlockOffset 372
#define IN_ParamBlockSize 6
#define IN_ParamCalcIndex(index) (index + IN_ParamBlockOffset + _channelIndex * IN_ParamBlockSize)

#define IN_fDeviceAddress                       0      // uint8_t
#define IN_fInstance                            1      // uint8_t
#define IN_fInputType                           2      // 8 Bits, Bit 7-0
#define IN_fFunction                            3      // 8 Bits, Bit 7-0
#define IN_fScene                               4      // uint8_t
#define IN_fThreshold                           5      // uint8_t

// DALI device short address
#define ParamIN_fDeviceAddress                      (knx.paramByte(IN_ParamCalcIndex(IN_fDeviceAddress)))
// Instance number
#define ParamIN_fInstance                           (knx.paramByte(IN_ParamCalcIndex(IN_fInstance)))
// Input type
#define ParamIN_fInputType                          (knx.paramByte(IN_ParamCalcIndex(IN_fInputType)))
// Push button function
#define ParamIN_fFunction                           (knx.paramByte(IN_ParamCalcIndex(IN_fFunction)))
// Scene number
#define ParamIN_fScene                              (knx.paramByte(IN_ParamCalcIndex(IN_fScene)))
// Send threshold
#define ParamIN_fThreshold                          (knx.paramByte(IN_ParamCalcIndex(IN_fThreshold)))

// deprecated
#define IN_KoOffset 1194

// Communication objects per channel (multiple occurrence)
#define IN_KoBlockOffset 1194
#define IN_KoBlockSize 4

#define IN_KoCalcNumber(index) (index + IN_KoBlockOffset + _channelIndex * IN_KoBlockSize)
#define IN_KoCalcIndex(number) ((number >= IN_KoCalcNumber(0) && number < IN_KoCalcNumber(IN_KoBlockSize)) ? (number - IN_KoBlockOffset) % IN_KoBlockSize : -1)
#define IN_KoCalcChannel(number) ((number >= IN_KoBlockOffset && number < IN_KoBlockOffset + IN_ChannelCount * IN_KoBlockSize) ? (number - IN_KoBlockOffset) / IN_KoBlockSize : -1)

#define IN_KoSwitch 0
#define IN_KoDimming 1
#define IN_KoIlluminance 2
#define IN_KoScene 3

// Input %C%: Switch/Occupancy
#define KoIN_Switch                              (knx.getGroupObject(IN_KoCalcNumber(IN_KoSwitch)))
// Input %C%: Dimming
#define KoIN_Dimming                             (knx.getGroupObject(IN_KoCalcNumber(IN_KoDimming)))
// Input %C%: Illuminance
#define KoIN_Illuminance                         (knx.getGroupObject(IN_KoCalcNumber(IN_KoIlluminance)))
// Input %C%: Scene
#define KoIN_Scene                               (knx.getGroupObject(IN_KoCalcNumber(IN_KoScene)))


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
