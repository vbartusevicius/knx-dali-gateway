from collections import defaultdict
from skidl import Pin, Part, Alias, SchLib, SKIDL, TEMPLATE

from skidl.pin import pin_types

SKIDL_lib_version = '0.0.1'

gen_circuits = SchLib(tool=SKIDL).add_parts(*[
        Part(**{ 'name':'GND', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'GND'}), 'ref_prefix':'#PWR', 'fplist':[''], 'footprint':'', 'keywords':'global power', 'description':'Power symbol creates a global label with name "GND" , ground', 'datasheet':'', 'pins':[
            Pin(num='1',func=pin_types.PWRIN,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'GNDA', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'GNDA'}), 'ref_prefix':'#PWR', 'fplist':[''], 'footprint':'', 'keywords':'global power', 'description':'Power symbol creates a global label with name "GNDA" , analog ground', 'datasheet':'', 'pins':[
            Pin(num='1',func=pin_types.PWRIN,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'GNDD', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'GNDD'}), 'ref_prefix':'#PWR', 'fplist':[''], 'footprint':'', 'keywords':'global power', 'description':'Power symbol creates a global label with name "GNDD" , digital ground', 'datasheet':'', 'pins':[
            Pin(num='1',func=pin_types.PWRIN,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'PWR_FLAG', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'PWR_FLAG'}), 'ref_prefix':'#FLG', 'fplist':[''], 'footprint':'', 'keywords':'flag power', 'description':'Special symbol for telling ERC where power comes from', 'datasheet':'', 'pins':[
            Pin(num='1',func=pin_types.PWROUT)], 'unit_defs':[] }),
        Part(**{ 'name':'Conn_01x02_Pin', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'Conn_01x02_Pin'}), 'ref_prefix':'J', 'fplist':[''], 'footprint':'Connector_PinHeader_1.00mm:PinHeader_1x02_P1.00mm_Vertical', 'keywords':'connector', 'description':'Generic connector, single row, 01x02, script generated', 'datasheet':'', 'pins':[
            Pin(num='1',name='Pin_1',func=pin_types.PASSIVE,unit=1),
            Pin(num='2',name='Pin_2',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'D_TVS', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'D_TVS'}), 'ref_prefix':'D', 'fplist':[''], 'footprint':'Diode_SMD:D_SMB', 'keywords':'diode TVS thyrector', 'description':'Bidirectional transient-voltage-suppression diode', 'datasheet':'', 'pins':[
            Pin(num='1',name='A1',func=pin_types.PASSIVE,unit=1),
            Pin(num='2',name='A2',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'D_Schottky', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'D_Schottky'}), 'ref_prefix':'D', 'fplist':[''], 'footprint':'Diode_SMD:D_SMA', 'keywords':'diode Schottky', 'description':'Schottky diode', 'datasheet':'', 'pins':[
            Pin(num='1',name='K',func=pin_types.PASSIVE,unit=1),
            Pin(num='2',name='A',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'NCN5130', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'NCN5130'}), 'ref_prefix':'U', 'fplist':[''], 'footprint':'Package_DFN_QFN:QFN-40-1EP_6x6mm_P0.5mm_EP4.6x4.6mm', 'keywords':'', 'description':'', 'datasheet':'https://www.onsemi.com/products/interfaces/wired-transceivers-modems/ncn5130', 'pins':[
            Pin(num='6',name='VBUS1',func=pin_types.PWRIN,unit=1),
            Pin(num='2',name='VBUS2',func=pin_types.PWRIN,unit=1),
            Pin(num='3',name='TX0',func=pin_types.OUTPUT,unit=1),
            Pin(num='4',name='CCP',func=pin_types.PASSIVE,unit=1),
            Pin(num='5',name='CAV',func=pin_types.PASSIVE,unit=1),
            Pin(num='7',name='CEQ1',func=pin_types.PASSIVE,unit=1),
            Pin(num='8',name='CEQ2',func=pin_types.PASSIVE,unit=1),
            Pin(num='9',name='VFILT',func=pin_types.PWROUT,unit=1),
            Pin(num='10',name='V20V',func=pin_types.PWROUT,unit=1),
            Pin(num='1',name='VSSA',func=pin_types.PWRIN,unit=1),
            Pin(num='16',name='VIN',func=pin_types.PWRIN,unit=1),
            Pin(num='17',name='VSW1',func=pin_types.OUTPUT,unit=1),
            Pin(num='18',name='VSS1',func=pin_types.PWRIN,unit=1),
            Pin(num='19',name='VDD1',func=pin_types.PWROUT,unit=1),
            Pin(num='20',name='VDD1M',func=pin_types.INPUT,unit=1),
            Pin(num='11',name='VDD2MV',func=pin_types.INPUT,unit=1),
            Pin(num='12',name='VDD2MC',func=pin_types.INPUT,unit=1),
            Pin(num='13',name='VDD2',func=pin_types.PWROUT,unit=1),
            Pin(num='14',name='VSS2',func=pin_types.PWRIN,unit=1),
            Pin(num='15',name='VSW2',func=pin_types.OUTPUT,unit=1),
            Pin(num='21',name='XCLKC',func=pin_types.INPUT,unit=1),
            Pin(num='22',name='TRIG',func=pin_types.OUTPUT,unit=1),
            Pin(num='23',name='MODE1',func=pin_types.INPUT,unit=1),
            Pin(num='24',name='MODE2',func=pin_types.INPUT,unit=1),
            Pin(num='25',name='TREQ',func=pin_types.INPUT,unit=1),
            Pin(num='26',name='CSB/UC1',func=pin_types.INPUT,unit=1),
            Pin(num='27',name='SDI/RXD',func=pin_types.INPUT,unit=1),
            Pin(num='28',name='SDO/TXD',func=pin_types.OUTPUT,unit=1),
            Pin(num='29',name='SCK/UC2',func=pin_types.OUTPUT,unit=1),
            Pin(num='30',name='VDDD',func=pin_types.PWRIN,unit=1),
            Pin(num='31',name='VSSD',func=pin_types.PWRIN,unit=1),
            Pin(num='32',name='XCLK',func=pin_types.OUTPUT,unit=1),
            Pin(num='33',name='XSEL',func=pin_types.INPUT,unit=1),
            Pin(num='34',name='XTAL2',func=pin_types.BIDIR,unit=1),
            Pin(num='35',name='XTAL1',func=pin_types.INPUT,unit=1),
            Pin(num='36',name='SAVEB',func=pin_types.OPENCOLL,unit=1),
            Pin(num='37',name='RESETB',func=pin_types.OPENCOLL,unit=1),
            Pin(num='38',name='FANIN',func=pin_types.INPUT,unit=1),
            Pin(num='39',name='ANAOUT',func=pin_types.OUTPUT,unit=1),
            Pin(num='40',name='VDDA',func=pin_types.PWRIN,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'R', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'R'}), 'ref_prefix':'R', 'fplist':[''], 'footprint':'Resistor_SMD:R_0603_1608Metric', 'keywords':'R res resistor', 'description':'Resistor', 'datasheet':'', 'pins':[
            Pin(num='1',func=pin_types.PASSIVE,unit=1),
            Pin(num='2',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'C', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'C'}), 'ref_prefix':'C', 'fplist':[''], 'footprint':'Capacitor_SMD:C_0603_1608Metric', 'keywords':'cap capacitor', 'description':'Unpolarized capacitor', 'datasheet':'', 'pins':[
            Pin(num='1',func=pin_types.PASSIVE,unit=1),
            Pin(num='2',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'L', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'L'}), 'ref_prefix':'L', 'fplist':[''], 'footprint':'Inductor_SMD:L_1210_3225Metric', 'keywords':'inductor choke coil reactor magnetic', 'description':'Inductor', 'datasheet':'', 'pins':[
            Pin(num='1',name='1',func=pin_types.PASSIVE,unit=1),
            Pin(num='2',name='2',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'AP63205WU', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'AP63205WU'}), 'ref_prefix':'U', 'fplist':['Package_TO_SOT_SMD:TSOT-23-6', 'Package_TO_SOT_SMD:TSOT-23-6'], 'footprint':'Package_TO_SOT_SMD:TSOT-23-6', 'keywords':'2A Buck DC/DC', 'description':'2A, 1.1MHz Buck DC/DC Converter, fixed 5.0V output voltage, TSOT-23-6', 'datasheet':'https://www.diodes.com/assets/Datasheets/AP63200-AP63201-AP63203-AP63205.pdf', 'pins':[
            Pin(num='1',name='FB',func=pin_types.INPUT,unit=1),
            Pin(num='2',name='EN',func=pin_types.INPUT,unit=1),
            Pin(num='3',name='IN',func=pin_types.PWRIN,unit=1),
            Pin(num='4',name='GND',func=pin_types.PWRIN,unit=1),
            Pin(num='5',name='SW',func=pin_types.OUTPUT,unit=1),
            Pin(num='6',name='BST',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'Screw_Terminal_01x02', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'Screw_Terminal_01x02'}), 'ref_prefix':'J', 'fplist':[''], 'footprint':'TerminalBlock:TerminalBlock_MaiXu_MX126-5.0-02P_1x02_P5.00mm', 'keywords':'screw terminal', 'description':'Generic screw terminal, single row, 01x02, script generated (kicad-library-utils/schlib/autogen/connector/)', 'datasheet':'', 'pins':[
            Pin(num='1',name='Pin_1',func=pin_types.PASSIVE,unit=1),
            Pin(num='2',name='Pin_2',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'DF06S', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'DF06S'}), 'ref_prefix':'D', 'fplist':['Diode_SMD:Diode_Bridge_OnSemi_SDIP-4L', 'Diode_SMD:Diode_Bridge_OnSemi_SDIP-4L'], 'footprint':'Diode_SMD:Diode_Bridge_Vishay_DFS', 'keywords':'rectifier acdc', 'description':'Bridge Rectifier, 420V Vrms, 1.5A If, SMDIP-4', 'datasheet':'https://www.onsemi.com/download/data-sheet/pdf/df10s-d.pdf', 'pins':[
            Pin(num='1',name='+',func=pin_types.PASSIVE,unit=1),
            Pin(num='2',name='-',func=pin_types.PASSIVE,unit=1),
            Pin(num='3',func=pin_types.PASSIVE,unit=1),
            Pin(num='4',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'LTV-352T', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'LTV-352T'}), 'ref_prefix':'U', 'fplist':['Package_SO:SO-4_4.4x3.6mm_P2.54mm'], 'footprint':'Package_SO:SOP-4_4.4x2.6mm_P1.27mm', 'keywords':'NPN Darlington DC Optocoupler', 'description':'DC Darlington Optocoupler, Vce 300V, CTR 1000%, SO-4', 'datasheet':'http://optoelectronics.liteon.com/upload/download/DS70-2001-002/S_110_LTV-352T%2020140520.pdf', 'pins':[
            Pin(num='1',func=pin_types.PASSIVE,unit=1),
            Pin(num='2',func=pin_types.PASSIVE,unit=1),
            Pin(num='3',func=pin_types.PASSIVE,unit=1),
            Pin(num='4',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'D_Zener', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'D_Zener'}), 'ref_prefix':'D', 'fplist':[''], 'footprint':'Diode_SMD:D_SOD-123', 'keywords':'diode', 'description':'Zener diode', 'datasheet':'', 'pins':[
            Pin(num='1',name='K',func=pin_types.PASSIVE,unit=1),
            Pin(num='2',name='A',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'2N7002', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'2N7002'}), 'ref_prefix':'Q', 'fplist':['', 'Package_TO_SOT_SMD:SOT-23'], 'footprint':'Package_TO_SOT_SMD:SOT-23', 'keywords':'N-Channel Switching MOSFET', 'description':'0.115A Id, 60V Vds, N-Channel MOSFET, SOT-23', 'datasheet':'https://www.onsemi.com/pub/Collateral/NDS7002A-D.PDF', 'pins':[
            Pin(num='1',name='G',func=pin_types.INPUT,unit=1),
            Pin(num='2',name='S',func=pin_types.PASSIVE,unit=1),
            Pin(num='3',name='D',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'BC847', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'BC847'}), 'ref_prefix':'Q', 'fplist':['', 'Package_TO_SOT_SMD:SOT-23'], 'footprint':'Package_TO_SOT_SMD:SOT-23', 'keywords':'NPN Small Signal Transistor', 'description':'0.1A Ic, 45V Vce, NPN Transistor, SOT-23', 'datasheet':'http://www.infineon.com/dgdl/Infineon-BC847SERIES_BC848SERIES_BC849SERIES_BC850SERIES-DS-v01_01-en.pdf?fileId=db3a304314dca389011541d4630a1657', 'pins':[
            Pin(num='1',name='B',func=pin_types.INPUT,unit=1),
            Pin(num='2',name='E',func=pin_types.PASSIVE,unit=1),
            Pin(num='3',name='C',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'Polyfuse', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'Polyfuse'}), 'ref_prefix':'F', 'fplist':[''], 'footprint':'Fuse:Fuse_1812_4532Metric', 'keywords':'resettable fuse PTC PPTC polyfuse polyswitch', 'description':'Resettable fuse, polymeric positive temperature coefficient', 'datasheet':'', 'pins':[
            Pin(num='1',func=pin_types.PASSIVE,unit=1),
            Pin(num='2',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'DCDC_ISO_4P', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'DCDC_ISO_4P'}), 'ref_prefix':'U', 'fplist':[''], 'footprint':'Converter_DCDC:Converter_DCDC_TRACO_TDN_5-xxxxWISM_SMD', 'keywords':'', 'description':'', 'datasheet':'TBD â\x80\x94 isolated DC-DC module, >=4 W class, 24 Vin', 'pins':[
            Pin(num='1',name='-Vin',func=pin_types.PWRIN,unit=1),
            Pin(num='2',name='+Vin',func=pin_types.PWRIN,unit=1),
            Pin(num='4',name='Remote',func=pin_types.INPUT,unit=1),
            Pin(num='5',name='-Vo',func=pin_types.PWROUT,unit=1),
            Pin(num='6',name='+Vo',func=pin_types.PWROUT,unit=1),
            Pin(num='7',name='Trim',func=pin_types.INPUT,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'LM2574HVM-ADJ', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'LM2574HVM-ADJ'}), 'ref_prefix':'U', 'fplist':['Package_SO:SOIC-14W_7.5x9mm_P1.27mm', 'Package_SO:SOIC-14W_7.5x9mm_P1.27mm'], 'footprint':'Package_SO:SOIC-14_3.9x8.7mm_P1.27mm', 'keywords':'Step-Down Voltage Regulator Adjustable 500mA HV', 'description':'Adjustable Output Voltage, 0.5A SIMPLE SWITCHERÂ® Step-Down Voltage Regulator, High Voltage Input, SO-8', 'datasheet':'https://www.ti.com/lit/ds/symlink/lm2574.pdf', 'pins':[
            Pin(num='3',name='FB',func=pin_types.INPUT,unit=1),
            Pin(num='4',name='SGND',func=pin_types.PWRIN,unit=1),
            Pin(num='5',name='~{ON}/OFF',func=pin_types.INPUT,unit=1),
            Pin(num='6',name='PGND',func=pin_types.PWRIN,unit=1),
            Pin(num='10',name='VIN',func=pin_types.PWRIN,unit=1),
            Pin(num='12',name='OUT',func=pin_types.OUTPUT,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'LM317_SOT-223', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'LM317_SOT-223'}), 'ref_prefix':'U', 'fplist':['Package_TO_SOT_SMD:SOT-223-3_TabPin2'], 'footprint':'Package_TO_SOT_SMD:SOT-223-3_TabPin2', 'keywords':'Adjustable Voltage Regulator 1A Positive', 'description':'1.5A 35V Adjustable Linear Regulator, SOT-223', 'datasheet':'http://www.ti.com/lit/ds/symlink/lm317.pdf', 'pins':[
            Pin(num='1',name='ADJ',func=pin_types.INPUT,unit=1),
            Pin(num='2',name='VO',func=pin_types.PWROUT,unit=1),
            Pin(num='3',name='VI',func=pin_types.PWRIN,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'SW_SPDT', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'SW_SPDT'}), 'ref_prefix':'SW', 'fplist':[''], 'footprint':'Button_Switch_SMD:SW_SPDT_PCM12', 'keywords':'switch single-pole double-throw spdt ON-ON', 'description':'Switch, single pole double throw', 'datasheet':'', 'pins':[
            Pin(num='1',name='A',func=pin_types.PASSIVE,unit=1),
            Pin(num='2',name='B',func=pin_types.PASSIVE,unit=1),
            Pin(num='3',name='C',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] }),
        Part(**{ 'name':'Conn_01x10_Pin', 'dest':TEMPLATE, 'tool':SKIDL, 'aliases':Alias({'Conn_01x10_Pin'}), 'ref_prefix':'J', 'fplist':[''], 'footprint':'Connector_JST:JST_SH_SM10B-SRSS-TB_1x10-1MP_P1.00mm_Horizontal', 'keywords':'connector', 'description':'Generic connector, single row, 01x10, script generated', 'datasheet':'', 'pins':[
            Pin(num='1',name='Pin_1',func=pin_types.PASSIVE,unit=1),
            Pin(num='2',name='Pin_2',func=pin_types.PASSIVE,unit=1),
            Pin(num='3',name='Pin_3',func=pin_types.PASSIVE,unit=1),
            Pin(num='4',name='Pin_4',func=pin_types.PASSIVE,unit=1),
            Pin(num='5',name='Pin_5',func=pin_types.PASSIVE,unit=1),
            Pin(num='6',name='Pin_6',func=pin_types.PASSIVE,unit=1),
            Pin(num='7',name='Pin_7',func=pin_types.PASSIVE,unit=1),
            Pin(num='8',name='Pin_8',func=pin_types.PASSIVE,unit=1),
            Pin(num='9',name='Pin_9',func=pin_types.PASSIVE,unit=1),
            Pin(num='10',name='Pin_10',func=pin_types.PASSIVE,unit=1)], 'unit_defs':[] })])