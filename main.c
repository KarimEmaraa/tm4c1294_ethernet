#include <stdint.h>
#include <stdbool.h>
#include "tm4c1294ncpdt.h"
#include "interrupt.h"
#include "pin_map.h"
#include "emac.h"
#include "inc/hw_emac.h"
#include "inc/hw_memmap.h"


#define NUM_TX_DESCRIPTORS (3U)
#define NUM_RX_DESCRIPTORS (3U)
#define BUFF_SIZE		   (1500U)

tEMACDMADescriptor RxDescriptor[NUM_TX_DESCRIPTORS];
tEMACDMADescriptor TxDescriptor[NUM_RX_DESCRIPTORS];

volatile uint8_t rxBuff1[BUFF_SIZE];
volatile uint8_t rxBuff2[BUFF_SIZE];
volatile uint8_t rxBuff3[BUFF_SIZE];

void PLL_init(void);
void emac_init(void);
void desc_init(void);
uint32_t RecieveHandler(void);
void TransmitHandler(uint32_t * const buf, uint32_t len);


void EthernetIntHandler(void)
{
	//get interrupt source and acknowledge it
	uint32_t temp = EMACIntStatus(EMAC0_BASE, true);

	EMACIntClear(EMAC0_BASE, temp);
	//if it's rx interrupt
	if(temp & EMAC_INT_RECEIVE)
	{
		RecieveHandler();
	}
}

int main(void)
{
	PLL_init();
	desc_init();
	emac_init();

	EMACIntClear(EMAC0_BASE, EMACIntStatus(EMAC0_BASE, false));
	IntEnable(INT_EMAC0);
	EMACIntEnable(EMAC0_BASE, EMAC_INT_RECEIVE);
	//// Mark the receive descriptors as available to the DMA to start
	// the receive processing.
	//
	for(uint32_t i = 0; i < NUM_RX_DESCRIPTORS; i++)
	{
		RxDescriptor[i].ui32CtrlStatus |= DES0_RX_CTRL_OWN;
	}


	while(1)
	{

	}

	return 0;
}


void PLL_init(void)
{

	// Power up the MOSC by clearing the NOXTAL bit in the MOSCCTL register.
	SYSCTL_MOSCCTL_R &= ~SYSCTL_MOSCCTL_NOXTAL;

	// Enable power to the main oscillator by clearing the power down bit.
	SYSCTL_MOSCCTL_R &= ~SYSCTL_MOSCCTL_PWRDN;

	// wait for the MOSCPUPRIS bit to be set in the Raw Interrupt Status (RIS), indicating MOSC crystal mode is ready.
	while ((SYSCTL_RIS_R & SYSCTL_RIS_MOSCPUPRIS) == 0) {}

	//Increase the drive strength for MOSC of 10MHz and above
	SYSCTL_MOSCCTL_R |= SYSCTL_MOSCCTL_OSCRNG;

	// Clear and set the PLL input clock source to be the MOSC.
	SYSCTL_RSCLKCFG_R = (SYSCTL_RSCLKCFG_R & ~SYSCTL_RSCLKCFG_PLLSRC_M) | SYSCTL_RSCLKCFG_PLLSRC_MOSC;

	// 25MHz in page 236
	SYSCTL_PLLFREQ1_R = 0x4;

	SYSCTL_PLLFREQ0_R &= ~SYSCTL_PLLFREQ0_MFRAC_M;

	// VCO = 480
	SYSCTL_PLLFREQ0_R = (SYSCTL_PLLFREQ0_R & ~SYSCTL_PLLFREQ0_MINT_M) | 0x60;

	// Power the PLL.
	SYSCTL_PLLFREQ0_R |= SYSCTL_PLLFREQ0_PLLPWR;

	// Wait until the PLL is powered and locked.
	while ((SYSCTL_PLLSTAT_R & SYSCTL_PLLSTAT_LOCK) == 0) {}

	// Set the timing parameters for the main Flash and EEPROM memories.
	uint32_t memtim0 = SYSCTL_MEMTIM0_R;
	memtim0 &= ~(SYSCTL_MEMTIM0_EBCHT_M | SYSCTL_MEMTIM0_EBCE | SYSCTL_MEMTIM0_EWS_M |
			SYSCTL_MEMTIM0_FBCHT_M | SYSCTL_MEMTIM0_FBCE | SYSCTL_MEMTIM0_FWS_M);

	memtim0 |= (SYSCTL_MEMTIM0_EBCHT_3_5 | SYSCTL_MEMTIM0_FBCHT_3_5 | (0x5 << SYSCTL_MEMTIM0_EWS_S) | 0x5);

	SYSCTL_MEMTIM0_R = memtim0;

	uint32_t rsclkcfg = SYSCTL_RSCLKCFG_R;

	// Set the PLL System Clock divisorSysClk = 480MHz / (1+3) = 120MHz.
	rsclkcfg = (rsclkcfg & ~SYSCTL_RSCLKCFG_PSYSDIV_M) | 0x3;

	// Use PLL as the system clock source
	rsclkcfg |= SYSCTL_RSCLKCFG_USEPLL;

	// Apply the MEMTIMU register value and upate the memory timings set in MEMTIM0.
	rsclkcfg |= SYSCTL_RSCLKCFG_MEMTIMU;

	SYSCTL_RSCLKCFG_R = rsclkcfg;
}

void emac_init(void)
{
	// dummy mac address
	uint8_t MACAddr[6] = { 0x0E, 0xDE, 0xAD ,0xBE, 0xEF, 0xDE };

	// the application should enable the clock to the Ethernet MAC by setting the R0 bit in SYSCTL_RCGCEMAC_R
	SYSCTL_RCGCEMAC_R |= SYSCTL_RCGCEMAC_R0 ;

	// hold until PREMAC == 0x1
	while(SYSCTL_PREMAC_R != 0x01U)
	{

	}
	// Enable the clock to the PHY module
	SYSCTL_RCGCEPHY_R |= SYSCTL_RCGCEPHY_R0;

	// hold until PREMAC == 0x1
	while(SYSCTL_PREPHY_R != 0x01U)
	{

	}
	// To hold the Ethernet PHY from transmitting energy on the line during configuration
	EMAC0_PC_R |= EMAC_PHY_INT_HOLD;

	// Enable power to the Ethernet PHY by setting the P0 bit in the PCEPHY
	SYSCTL_PCEPHY_R |= SYSCTL_PCEPHY_P0;

	while(SYSCTL_PREPHY_R != 0x01U)
	{

	}

	// Configure emac for mixed burst, with 8 words descriptor size with burst length = 4 words
	EMAC0_DMABUSMOD_R |=  EMAC_BCONFIG_FIXED_BURST | (4 << 8) | EMAC_DMABUSMOD_ATDS;

	// Configure RX Descriptor list address
	EMAC0_RXDLADDR_R = (uint32_t)RxDescriptor;

	// Configure TX Descriptor list address
	EMAC0_TXDLADDR_R = (uint32_t)TxDescriptor;

	// Drop VLAN Tagged frames that doesnt match VLAN tag and forward control frames to application and multicast frames
	EMAC0_FRAMEFLTR_R |= EMAC_FRAMEFLTR_VTFE | EMAC_FRAMEFLTR_PCF_ALL | EMAC_FRAMEFLTR_PM;

	// Use MAC address insertion From EMACADDR0, 10mbps, full duplex, Disable retry on failed tx, enable tx and rx
	EMAC0_CFG_R |= EMAC_CONFIG_SA_INSERT | EMAC_CONFIG_USE_MACADDR0 | EMAC_CFG_DUPM | EMAC_CFG_DR | EMAC_CFG_TE | EMAC_CFG_RE;

	// Configure mac address in EMACADDR0
	EMAC0_ADDR0H_R = (MACAddr[5] << 8) | MACAddr[4];
	EMAC0_ADDR0L_R = (MACAddr[3] << 24) | (MACAddr[2] << 16) | (MACAddr[1] << 8) | MACAddr[0];

	//Enable DMA to traverse TX and RX descriptors, 64 fifo buffer threeshold
	EMAC0_DMAOPMODE_R |= EMAC_DMAOPMODE_ST | EMAC_DMAOPMODE_SR;

}

void desc_init(void)
{
	//init each tx descriptor
	for (int i = 0; i < NUM_TX_DESCRIPTORS; ++i)
	{
		//insert the source address in frame by hw
		TxDescriptor[i].ui32Count |= DES1_TX_CTRL_SADDR_INSERT;
		// chained(linked list mode), with interrupt enable on tx, buf contains all frame, enable checksum on all frame
		TxDescriptor[i].ui32CtrlStatus = DES0_TX_CTRL_CHAINED | DES0_TX_CTRL_INTERRUPT | DES0_TX_CTRL_FIRST_SEG | DES0_TX_CTRL_LAST_SEG | DES0_TX_CTRL_IP_ALL_CKHSUMS;
		// link all descriptors.
		if(i + 1 == NUM_TX_DESCRIPTORS)
		{
			TxDescriptor[i].DES3.pLink = &TxDescriptor[0];
		}
		else
		{
			TxDescriptor[i].DES3.pLink = &TxDescriptor[i + 1];
		}
	}

	//init each rx descriptor
	for (int i = 0; i < NUM_RX_DESCRIPTORS; ++i)
	{
		// chained (linked list) and configure for BUFF SIZE
		RxDescriptor[i].ui32Count |= DES1_RX_CTRL_CHAINED | (BUFF_SIZE << DES1_RX_CTRL_BUFF1_SIZE_S);
		// Clear all bits
		RxDescriptor[i].ui32CtrlStatus = 0;
		// link all descriptors.
		if(i + 1 == NUM_RX_DESCRIPTORS)
		{
			RxDescriptor[i].DES3.pLink = &RxDescriptor[0];
		}
		else
		{
			RxDescriptor[i].DES3.pLink = &RxDescriptor[i + 1];
		}
	}
}


uint32_t RecieveHandler(void)
{
	uint32_t frameLength = 0;
	for(uint32_t i = 0; i < NUM_RX_DESCRIPTORS; i++)
	{
		//check if s/w owns the descriptor
		if(!(RxDescriptor[i].ui32CtrlStatus & DES0_RX_CTRL_OWN))
		{
			//check if no errors
			if(!(RxDescriptor[i].ui32CtrlStatus & DES0_RX_STAT_ERR))
			{
				//get size of recieved frame
				frameLength = ((RxDescriptor[i].ui32CtrlStatus & DES0_RX_STAT_FRAME_LENGTH_M) >> DES0_RX_STAT_FRAME_LENGTH_S);

				//dump frame contents to uart for example.
				//TODO
			}
			//set desc back to hw
			RxDescriptor[i].ui32CtrlStatus = DES0_RX_CTRL_OWN;

		}
	}
	// Return the Frame Length
	return frameLength;
}

void Transmit_Handler(uint32_t * const buf, uint32_t len)
{
	static uint32_t txIndex = 0;

	//wait until the current t desc is owned by sw i.e current transmission is done
	while(TxDescriptor[txIndex].ui32CtrlStatus &
			DES0_TX_CTRL_OWN)
	{

	}
	//use next desc
	txIndex++;
	if(txIndex == NUM_TX_DESCRIPTORS)
	{
		txIndex = 0;
	}
	//set buffer length
	TxDescriptor[txIndex].ui32Count = (uint32_t)len;
	//set buffer pointer
	TxDescriptor[txIndex].pvBuffer1 = buf;
	//set this desc as a whole frame, and enable checksum on all frame, with interrupt on completion with chained mode
	TxDescriptor[txIndex].ui32CtrlStatus = (DES0_TX_CTRL_LAST_SEG | DES0_TX_CTRL_FIRST_SEG |
			DES0_TX_CTRL_INTERRUPT | DES0_TX_CTRL_IP_ALL_CKHSUMS |
			DES0_TX_CTRL_CHAINED | DES0_TX_CTRL_OWN);

	EMAC0_DMAOPMODE_R |= EMAC_DMAOPMODE_ST;
	//
	// Return the number of bytes sent.
	//
}

