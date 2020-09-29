#include <xc.h>
#include <stdint.h>
#include <pic.h>
#include <pic16f1828.h>
#include <stdbool.h>

#pragma config MCLRE = OFF, CP = OFF, WDTE = OFF, FOSC = INTOSC

// SCS = 1x, System Clock determined by IRCF<3:0> bits
// IRCF = 1110, IRCF frequency set to 8MHz
#define OSC_FREQ        0b01110010
#define _XTAL_FREQ      8000000

//SPI Pin Definitions
#define SS              RC6
#define SDO             RC7
#define SCK             RB6

//PIC GPIO Definitions
#define CTRL8               RA4
#define CTRL9               RA5
#define MODE_SWITCH         RB4
#define CTRL_BYPASS         RB5
#define SET_SWITCH          RB7
#define BYPASS_SWITCH       RC0
#define TUBESCREAMER_SWITCH RC1
#define TUBE_DRIVE_SWITCH   RC2
#define EXT_SWITCH          RC3
#define DELAY_SWITCH        RC4
#define CHORUS_SWITCH       RC5

//MCP23S17 Register Definitions
#define IODIRA          0x00    //IODIRA Address
#define IODIRB          0x01    //IODIRB Address
#define IOCON           0x0A    //Configuration Register, 0b10100000
#define GPIOA           0x12    //GPIO Address - 8-bit wide bidirectional port A
#define GPIOB           0x13    //GPIO Address - 8-bit wide bidirectional port B

//GPA Masks
#define LED_PROGRAM_4       0b00000001          //GPA0 - MCP Pin 21
#define LED_PROGRAM_5       0b00000010          //GPA1 - MCP Pin 22
#define LED_SET             0b00000100          //GPA2 - MCP Pin 23
#define CTRL_TUBESCREAMER   0b00001000          //GPA3 - MCP Pin 24
#define CTRL_TUBE_DRIVE     0b00010000          //GPA4 - MCP Pin 25
#define CTRL_EXT            0b00100000          //GPA5 - MCP Pin 26
#define CTRL_DELAY          0b01000000          //GPA6 - MCP Pin 27
#define CTRL_CHORUS         0b10000000          //GPA7 - MCP Pin 28

//GPB Masks
#define LED_PROGRAM_1       0b00000001          //GPB0 - MCP Pin 1
#define LED_PROGRAM_2       0b00000010          //GPB1 - MCP Pin 2
#define LED_PROGRAM_3       0b00000100          //GPB2 - MCP Pin 3
#define LED_TUBESCREAMER    0b00001000          //GPB3 - MCP Pin 4
#define LED_TUBE_DRIVE      0b00010000          //GPB4 - MCP Pin 5
#define LED_EXT             0b00100000          //GPB5 - MCP Pin 6
#define LED_DELAY           0b01000000          //GPB6 - MCP Pin 7
#define LED_CHORUS          0b10000000          //GPB7 - MCP Pin 8

//Presets
#define PRESET_1A           0x01
#define PRESET_1B           0x05
#define PRESET_2A           0x09
#define PRESET_2B           0x0D
#define PRESET_3A           0x11
#define PRESET_3B           0x15
#define PRESET_4A           0x19
#define PRESET_4B           0x1D
#define PRESET_5A           0x21
#define PRESET_5B           0x25



#define SELECT_MODE         1
#define PLAY_MODE           0


// Function Definitions
void updateDelaySetpoint();
void clearEffectLEDs();
void SPI_Write(unsigned char address, unsigned char data);
unsigned char EEPROM_Read(unsigned char addr);
void EEPROM_Write(unsigned char addr, unsigned char data);



void main(){
    
    // Variable Initialization
    uint8_t         MCP_GPIOA = 0xFF;
    uint8_t         MCP_GPIOB = 0xFF;
    unsigned char   result = 0x00;
    uint8_t         db_cnt = 0;
    
    uint8_t         mode = SELECT_MODE;
    bool            bypass_update_flag = false;
    
    // Setup oscillator
    OSCCON = OSC_FREQ;
    
    // Disable all analog functions
    ANSELA = 0b00000000;
    ANSELB = 0b00000000;
    ANSELC = 0b00000000;
    
    // Set Delay Slide Switch Analog Input
    ANSELAbits.ANSA2 = 1;    
    
    //GPIOA bit I/O Configurations 
    TRISAbits.TRISA0 = 1;       //Programming Port - PGD
    TRISAbits.TRISA1 = 1;       //Programming Port - PGC
    TRISAbits.TRISA2 = 1;       //Delay Slide Switch Analog Input
                                //RA3 = MCLR Pin
    TRISAbits.TRISA4 = 0;       //Delay Setpoint #2 
    TRISAbits.TRISA5 = 0;       //Delay Setpoint #1 
    
    //GPIOB bit I/O Configurations
    TRISBbits.TRISB4 = 1;       //Mode Switch Input
    TRISBbits.TRISB5 = 0;       //Bypass Control Output
    TRISBbits.TRISB6 = 0;       //SPI SCK Clock Output
    TRISBbits.TRISB7 = 1;       //Set Switch Input
    
    //GPIOC bit I/O Configurations
    TRISCbits.TRISC0 = 1;       //Bypass Switch Input
    TRISCbits.TRISC1 = 1;       //Tube Screamer Switch Input
    TRISCbits.TRISC2 = 1;       //Tube Drive Switch Input
    TRISCbits.TRISC3 = 1;       //EXT Switch Input
    TRISCbits.TRISC4 = 1;       //Delay Switch Input
    TRISCbits.TRISC5 = 1;       //Chorus Switch Input
    TRISCbits.TRISC6 = 0;       //RC6 = SPI Slave Select
    TRISCbits.TRISC7 = 0;       //RC7 = SPI Slave Data Out
    
    // Set Analog Conversion Clock
    ADCON1bits.ADCS = 0b100;
    ADCON1bits.ADNREF = 0;
    ADCON1bits.ADPREF = 0b00;
    ADCON0bits.CHS = 0b0010;
    ADCON0bits.ADON = 1;
    
    // Initialize PIC SPI Peripheral
    SSP1CONbits.SSPEN = 0;
    SSP1CONbits.CKP = 0;
    SSP1CONbits.SSPM = 0b0000;
    SSP1STATbits.CKE = 1;
    SSP1STATbits.SMP = 1;
    SSP1CONbits.SSPEN = 1;
    
    // Initialize MCP23S17 SPI I/O Expander
    SS = 1;
    __delay_ms(10);
    SPI_Write(IOCON, 0b00101000);   // I/O Control Register: BANK=0, SEQOP=1, HAEN=1 (Enable Addressing)
    SPI_Write(IODIRA, 0b00000000);  // GPIOA As Output
    SPI_Write(IODIRB, 0b00000000);  // GPIOB As Output
    SPI_Write(GPIOA, MCP_GPIOA);   // Reset Output on GPIOA
    SPI_Write(GPIOB, MCP_GPIOB);   // Reset Output on GPIOB
    
    
    // Initialize Controls
    CTRL_BYPASS = 0;                    // Init to Bypass
    uint8_t         bypass = 0;
    
    // Select Mode
    MCP_GPIOA = ~(~(MCP_GPIOA) | LED_SET);
    
    
    // Initialize GPIO in MCP Module
    SPI_Write(GPIOA, MCP_GPIOA);
    SPI_Write(GPIOB, MCP_GPIOB);
    
    for(;;){        
        for(db_cnt = 0; db_cnt <= 10; db_cnt++){
            updateDelaySetpoint();
            __delay_ms(2);
            if (BYPASS_SWITCH == 1 && TUBESCREAMER_SWITCH == 1 && TUBE_DRIVE_SWITCH == 1 && EXT_SWITCH == 1 && DELAY_SWITCH == 1 && CHORUS_SWITCH == 1 && SET_SWITCH == 1 && MODE_SWITCH == 1){
                db_cnt = 0;
            }
        }
        
        // Determine Bypass Mode
        if(BYPASS_SWITCH == 0){
            if(bypass == 0){
                bypass = 1;
                CTRL_BYPASS = 0;
            }
            else{
                bypass = 0;
                CTRL_BYPASS = 1;
            }
            bypass_update_flag = true;
        }
        
        // Determine State
        else if(MODE_SWITCH == 0){
            if(mode == PLAY_MODE){
                MCP_GPIOB = MCP_GPIOB | LED_PROGRAM_1;
                MCP_GPIOB = MCP_GPIOB | LED_PROGRAM_2;
                MCP_GPIOB = MCP_GPIOB | LED_PROGRAM_3;
                MCP_GPIOA = MCP_GPIOA | LED_PROGRAM_4;
                MCP_GPIOA = MCP_GPIOA | LED_PROGRAM_5;
                mode = SELECT_MODE;
                MCP_GPIOA = ~(~(MCP_GPIOA) | LED_SET);
            }
            else{
                mode = PLAY_MODE;
                MCP_GPIOA = EEPROM_Read(PRESET_1A);
                MCP_GPIOB = EEPROM_Read(PRESET_1B);
                MCP_GPIOB = ~(~(MCP_GPIOB) | LED_PROGRAM_1);
                MCP_GPIOA = MCP_GPIOA | LED_SET;
            }
        }
        
        else if(SET_SWITCH == 0){
            if(mode == SELECT_MODE){
                
                MCP_GPIOA = ~(~(MCP_GPIOA) | 0b00000011);
                MCP_GPIOB = ~(~(MCP_GPIOB) | 0b00000111);
                SPI_Write(GPIOA, MCP_GPIOA);
                SPI_Write(GPIOB, MCP_GPIOB);
                
                for(db_cnt = 0; db_cnt <= 10; db_cnt++){
                    __delay_ms(2);
                    if (BYPASS_SWITCH == 1 && TUBESCREAMER_SWITCH == 1 && TUBE_DRIVE_SWITCH == 1 && EXT_SWITCH == 1 && DELAY_SWITCH == 1 && CHORUS_SWITCH == 1){
                        db_cnt = 0;
                    }
                }
                
                MCP_GPIOA = MCP_GPIOA | 0b00000011;
                MCP_GPIOB = MCP_GPIOB | 0b00000111;
                SPI_Write(GPIOA, MCP_GPIOA);
                SPI_Write(GPIOB, MCP_GPIOB);
                
                if(TUBESCREAMER_SWITCH == 0){
                    //blink PGM LED 1
                    MCP_GPIOB = ~((~MCP_GPIOB) | LED_PROGRAM_1);
                    SPI_Write(GPIOB, MCP_GPIOB);
                    __delay_ms(250);
                    MCP_GPIOB = MCP_GPIOB | LED_PROGRAM_1;
                    SPI_Write(GPIOB, MCP_GPIOB);
                    __delay_ms(250);
                    MCP_GPIOB = ~((~MCP_GPIOB) | LED_PROGRAM_1);
                    SPI_Write(GPIOB, MCP_GPIOB);
                    __delay_ms(250);
                    MCP_GPIOB = MCP_GPIOB | LED_PROGRAM_1;
                    SPI_Write(GPIOB, MCP_GPIOB);
                    
                    EEPROM_Write(PRESET_1A, MCP_GPIOA);
                    __delay_ms(20);
                    EEPROM_Write(PRESET_1B, MCP_GPIOB);
                    __delay_ms(20);
                    
                    
                }
                else if(TUBE_DRIVE_SWITCH == 0){
                    //blink PGM LED 2
                    MCP_GPIOB = ~((~MCP_GPIOB) | LED_PROGRAM_2);
                    SPI_Write(GPIOB, MCP_GPIOB);
                    __delay_ms(250);
                    MCP_GPIOB = MCP_GPIOB | LED_PROGRAM_2;
                    SPI_Write(GPIOB, MCP_GPIOB);
                    __delay_ms(250);
                    MCP_GPIOB = ~((~MCP_GPIOB) | LED_PROGRAM_2);
                    SPI_Write(GPIOB, MCP_GPIOB);
                    __delay_ms(250);
                    MCP_GPIOB = MCP_GPIOB | LED_PROGRAM_2;
                    SPI_Write(GPIOB, MCP_GPIOB);
                    
                    EEPROM_Write(PRESET_2A, MCP_GPIOA);
                    __delay_ms(20);
                    EEPROM_Write(PRESET_2B, MCP_GPIOB);
                    __delay_ms(20);
                }
                else if(EXT_SWITCH == 0){
                    //blink PGM LED 3
                    MCP_GPIOB = ~((~MCP_GPIOB) | LED_PROGRAM_3);
                    SPI_Write(GPIOB, MCP_GPIOB);
                    __delay_ms(250);
                    MCP_GPIOB = MCP_GPIOB | LED_PROGRAM_3;
                    SPI_Write(GPIOB, MCP_GPIOB);
                    __delay_ms(250);
                    MCP_GPIOB = ~((~MCP_GPIOB) | LED_PROGRAM_3);
                    SPI_Write(GPIOB, MCP_GPIOB);
                    __delay_ms(250);
                    MCP_GPIOB = MCP_GPIOB | LED_PROGRAM_3;
                    SPI_Write(GPIOB, MCP_GPIOB);
                    
                    EEPROM_Write(PRESET_3A, MCP_GPIOA);
                    __delay_ms(20);
                    EEPROM_Write(PRESET_3B, MCP_GPIOB);
                    __delay_ms(20);
                }
                else if(DELAY_SWITCH == 0){
                    //blink PGM LED 4
                    MCP_GPIOA = ~((~MCP_GPIOA) | LED_PROGRAM_4);
                    SPI_Write(GPIOA, MCP_GPIOA);
                    __delay_ms(250);
                    MCP_GPIOA = MCP_GPIOA | LED_PROGRAM_4;
                    SPI_Write(GPIOA, MCP_GPIOA);
                    __delay_ms(250);
                    MCP_GPIOA = ~((~MCP_GPIOA) | LED_PROGRAM_4);
                    SPI_Write(GPIOA, MCP_GPIOA);
                    __delay_ms(250);
                    MCP_GPIOA = MCP_GPIOA | LED_PROGRAM_4;
                    SPI_Write(GPIOA, MCP_GPIOA);
                    
                    EEPROM_Write(PRESET_4A, MCP_GPIOA);
                    __delay_ms(20);
                    EEPROM_Write(PRESET_4B, MCP_GPIOB);
                    __delay_ms(20);
                }
                else if(CHORUS_SWITCH == 0){
                    //blink PGM LED 5
                    MCP_GPIOA = ~((~MCP_GPIOA) | LED_PROGRAM_5);
                    SPI_Write(GPIOA, MCP_GPIOA);
                    __delay_ms(250);
                    MCP_GPIOA = MCP_GPIOA | LED_PROGRAM_5;
                    SPI_Write(GPIOA, MCP_GPIOA);
                    __delay_ms(250);
                    MCP_GPIOA = ~((~MCP_GPIOA) | LED_PROGRAM_5);
                    SPI_Write(GPIOA, MCP_GPIOA);
                    __delay_ms(250);
                    MCP_GPIOA = MCP_GPIOA | LED_PROGRAM_5;
                    SPI_Write(GPIOA, MCP_GPIOA);
                    
                    EEPROM_Write(PRESET_5A, MCP_GPIOA);
                    __delay_ms(20);
                    EEPROM_Write(PRESET_5B, MCP_GPIOB);
                    __delay_ms(20);
                }
                
                for(db_cnt = 0; db_cnt <= 10; db_cnt++){
                    __delay_ms(2);
                    if (BYPASS_SWITCH == 0 || TUBESCREAMER_SWITCH == 0 || TUBE_DRIVE_SWITCH == 0 || EXT_SWITCH == 0 || DELAY_SWITCH == 0 || CHORUS_SWITCH == 0){
                        db_cnt = 0;
                    }
                }
            }
            
            
        }
        
        
        if(mode == SELECT_MODE && bypass_update_flag == false){
            if(TUBESCREAMER_SWITCH == 0){
                if((MCP_GPIOB & LED_TUBESCREAMER) == 0){
                    MCP_GPIOB = MCP_GPIOB | LED_TUBESCREAMER;
                    MCP_GPIOA = MCP_GPIOA | CTRL_TUBESCREAMER;
                }
                else{
                    MCP_GPIOB = ~(~(MCP_GPIOB) | LED_TUBESCREAMER);
                    MCP_GPIOA = ~(~(MCP_GPIOA) | CTRL_TUBESCREAMER);
                }
            }
            else if(TUBE_DRIVE_SWITCH == 0){
                if((MCP_GPIOB & LED_TUBE_DRIVE) == 0){
                    MCP_GPIOB = MCP_GPIOB | LED_TUBE_DRIVE;
                    MCP_GPIOA = MCP_GPIOA | CTRL_TUBE_DRIVE;
                }
                else{
                    MCP_GPIOB = ~(~(MCP_GPIOB) | LED_TUBE_DRIVE);
                    MCP_GPIOA = ~(~(MCP_GPIOA) | CTRL_TUBE_DRIVE);
                }
            }
            else if(EXT_SWITCH == 0){
                if((MCP_GPIOB & LED_EXT) == 0){
                    MCP_GPIOB = MCP_GPIOB | LED_EXT;
                    MCP_GPIOA = MCP_GPIOA | CTRL_EXT;
                }
                else{
                    MCP_GPIOB = ~(~(MCP_GPIOB) | LED_EXT);
                    MCP_GPIOA = ~(~(MCP_GPIOA) | CTRL_EXT);
                }
            }
            else if(DELAY_SWITCH == 0){
                if((MCP_GPIOB & LED_DELAY) == 0){
                    MCP_GPIOB = MCP_GPIOB | LED_DELAY;
                    MCP_GPIOA = MCP_GPIOA | CTRL_DELAY;
                }
                else{
                    MCP_GPIOB = ~(~(MCP_GPIOB) | LED_DELAY);
                    MCP_GPIOA = ~(~(MCP_GPIOA) | CTRL_DELAY);
                }
            }
            else if(CHORUS_SWITCH == 0){
                if((MCP_GPIOB & LED_CHORUS) == 0){
                    MCP_GPIOB = MCP_GPIOB | LED_CHORUS;
                    MCP_GPIOA = MCP_GPIOA | CTRL_CHORUS;
                }
                else{
                    MCP_GPIOB = ~(~(MCP_GPIOB) | LED_CHORUS);
                    MCP_GPIOA = ~(~(MCP_GPIOA) | CTRL_CHORUS);
                }
            }
        }
        
        else if(mode == PLAY_MODE && bypass_update_flag == false){
            MCP_GPIOB = MCP_GPIOB | LED_PROGRAM_1;
            MCP_GPIOB = MCP_GPIOB | LED_PROGRAM_2;
            MCP_GPIOB = MCP_GPIOB | LED_PROGRAM_3;
            MCP_GPIOA = MCP_GPIOA | LED_PROGRAM_4;
            MCP_GPIOA = MCP_GPIOA | LED_PROGRAM_5;
            
            if(TUBESCREAMER_SWITCH == 0){
                MCP_GPIOA = EEPROM_Read(PRESET_1A);
                MCP_GPIOB = EEPROM_Read(PRESET_1B);
                MCP_GPIOB = ~(~(MCP_GPIOB) | LED_PROGRAM_1);
                MCP_GPIOA = MCP_GPIOA | LED_SET;
                
            }
            else if(TUBE_DRIVE_SWITCH == 0){
                MCP_GPIOA = EEPROM_Read(PRESET_2A);
                MCP_GPIOB = EEPROM_Read(PRESET_2B);
                MCP_GPIOB = ~(~(MCP_GPIOB) | LED_PROGRAM_2);
                MCP_GPIOA = MCP_GPIOA | LED_SET;
            }
            else if(EXT_SWITCH == 0){
                MCP_GPIOA = EEPROM_Read(PRESET_3A);
                MCP_GPIOB = EEPROM_Read(PRESET_3B);
                MCP_GPIOB = ~(~(MCP_GPIOB) | LED_PROGRAM_3);
                MCP_GPIOA = MCP_GPIOA | LED_SET;
            }
            else if(DELAY_SWITCH == 0){
                MCP_GPIOA = EEPROM_Read(PRESET_4A);
                MCP_GPIOB = EEPROM_Read(PRESET_4B);
                MCP_GPIOA = ~(~(MCP_GPIOA) | LED_PROGRAM_4);
                MCP_GPIOA = MCP_GPIOA | LED_SET;
            }
            else if(CHORUS_SWITCH == 0){
                MCP_GPIOA = EEPROM_Read(PRESET_5A);
                MCP_GPIOB = EEPROM_Read(PRESET_5B);
                MCP_GPIOA = ~(~(MCP_GPIOA) | LED_PROGRAM_5);
                MCP_GPIOA = MCP_GPIOA | LED_SET;
            }
            else if(MODE_SWITCH == 0){
                MCP_GPIOA = EEPROM_Read(PRESET_1A);
                MCP_GPIOB = EEPROM_Read(PRESET_1B);
                MCP_GPIOB = ~(~(MCP_GPIOB) | LED_PROGRAM_1);
                MCP_GPIOA = MCP_GPIOA | LED_SET;
            }
        }
         
        
        // Update GPIO in MCP Module
        SPI_Write(GPIOA, MCP_GPIOA);
        SPI_Write(GPIOB, MCP_GPIOB);
        bypass_update_flag = false;
        
        //DEBOUNCE LOOP
        for(db_cnt = 0; db_cnt <= 10; db_cnt++){
            __delay_ms(2);
            if (BYPASS_SWITCH == 0 || TUBESCREAMER_SWITCH == 0 || TUBE_DRIVE_SWITCH == 0 || EXT_SWITCH == 0 || DELAY_SWITCH == 0 || CHORUS_SWITCH == 0 || SET_SWITCH == 0 || MODE_SWITCH == 0){
                db_cnt = 0;
            }
        }
        
        
    }
}






void updateDelaySetpoint(){
    ADCON0bits.GO = 1;
    while(ADCON0bits.GO_nDONE)
        ;
    __delay_ms(20);
    if(ADRESH < 0x1F){
        CTRL8 = 1;
        CTRL9 = 1;
    }

    if(ADRESH >= 0x1F && ADRESH < 0xF0){
        CTRL8 = 1;
        CTRL9 = 0;
    }

    if(ADRESH >= 0xF0){
        CTRL8 = 0;
        CTRL9 = 1;
    }
}

//SPI Write Function
void SPI_Write(unsigned char address, unsigned char data){
    if(SSP1CONbits.WCOL == 1){
        SSP1CONbits.WCOL = 0;
    }
    
    SS = 0;
    for (uint8_t j = 0; j < 16; ++j);
    SSP1BUF = 0b01000000;
    while(!SSP1STATbits.BF);
    SSP1BUF = address;
    while(!SSP1STATbits.BF);
    SSP1BUF = data;
    while(!SSP1STATbits.BF);
    SS = 1;
    for (uint8_t j = 0; j < 16; ++j);
}

//EEPROM Read Function
unsigned char EEPROM_Read(unsigned char addr){
    EEADRL = addr;
    EECON1bits.EEPGD = 0;
    EECON1bits.CFGS = 0;
    EECON1bits.RD = 1;
    return EEDATL;
}

//EEPROM Write Function
void EEPROM_Write(unsigned char addr, unsigned char data){
    while(EECON1bits.WR){
        continue;
    }
    EEADRL = addr;
    EEDATL = data;
    EECON1bits.EEPGD = 0;
    EECON1bits.CFGS = 0;
    STATUSbits.CARRY = 0;
    if(INTCONbits.GIE){
        STATUSbits.CARRY = 1;
    }
    INTCONbits.GIE = 0;
    EECON1bits.WREN = 1;
    EECON2 = 0x55;
    EECON2 = 0xAA;
    EECON1bits.WR = 1;
    EECON1bits.WREN = 0;
    
    if(STATUSbits.CARRY){
        INTCONbits.GIE = 1;
    }
    PIR2bits.EEIF = 0;
}
