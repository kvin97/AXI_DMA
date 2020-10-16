#include <stdio.h>
#include "platform.h"
#include "xparameters.h"
#include "xaxidma.h"
#include "sleep.h"
#include "xil_cache.h"

#define MEM_BASE_ADDR		(XPAR_PSU_DDR_0_S_AXI_BASEADDR + 0x1000000)

#define TX_BD_SPACE_BASE	(MEM_BASE_ADDR)
#define TX_BD_SPACE_HIGH	(MEM_BASE_ADDR + 0x00000FFF)

#define RX_BD_SPACE_BASE	(MEM_BASE_ADDR + 0x00001000)
#define RX_BD_SPACE_HIGH	(MEM_BASE_ADDR + 0x00001FFF)
#define TX_BUFFER_BASE		(MEM_BASE_ADDR + 0x00100000)
#define RX_BUFFER_BASE		(MEM_BASE_ADDR + 0x00300000)
#define RX_BUFFER_HIGH		(MEM_BASE_ADDR + 0x004FFFFF)

#define MAX_PKT_LEN 0x1E
#define BD_SPACE_SIZE 0x08

static int TxSetup(XAxiDma * AxiDmaInstPtr);
static int SendPacket(XAxiDma * AxiDmaInstPtr, u32 * TxArray);

int main(){

	u32 Status;

	u32 a[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
				22, 23, 24, 25, 26, 27, 28, 29, 30};

	XAxiDma DMA_HW;
	XAxiDma_Config *AXI_DMA_CONFIG;

	AXI_DMA_CONFIG =  XAxiDma_LookupConfigBaseAddr(XPAR_AXI_DMA_0_BASEADDR); // XAxiDma_LookupConfig(XPAR_AXI_DMA_0_DEVICE_ID);

	Status = XAxiDma_CfgInitialize(&DMA_HW, AXI_DMA_CONFIG);

	if (Status != XST_SUCCESS) {
		xil_printf("DMA initialization status : %d\n", XST_FAILURE);
	}

	if(!XAxiDma_HasSg(&DMA_HW)) {
		xil_printf("Device configured as Simple mode \r\n");

		return XST_FAILURE;
	}

	Xil_DCacheFlushRange( (UINTPTR) a, sizeof(u32)*30);

	/* Setup Transmission */
	Status = TxSetup(&DMA_HW);

	if (Status != XST_SUCCESS) {
		xil_printf("Tx Setup status : %d\n", XST_FAILURE);
	}

	/* Send a packet */
	Status = SendPacket(&DMA_HW, a);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	return 0;
}

static int TxSetup(XAxiDma * AxiDmaInstPtr)
{
	XAxiDma_BdRing *TxRingPtr;
	XAxiDma_Bd BdTemplate;
	int Delay = 0;
	int Coalesce = 1;
	int Status;
	u32 BdCount;

	TxRingPtr = XAxiDma_GetTxRing(AxiDmaInstPtr);

	/* Disable all TX interrupts before TxBD space setup */

	XAxiDma_BdRingIntDisable(TxRingPtr, XAXIDMA_IRQ_ALL_MASK);

	/* Set TX delay and coalesce */
	XAxiDma_BdRingSetCoalesce(TxRingPtr, Coalesce, Delay);

	/* Setup TxBD space  */
	BdCount = XAxiDma_BdRingCntCalc(XAXIDMA_BD_MINIMUM_ALIGNMENT,
				TX_BD_SPACE_HIGH - TX_BD_SPACE_BASE + 1);

	Status = XAxiDma_BdRingCreate(TxRingPtr, TX_BD_SPACE_BASE,
				TX_BD_SPACE_BASE,
				XAXIDMA_BD_MINIMUM_ALIGNMENT, BdCount);
	if (Status != XST_SUCCESS) {
		xil_printf("failed create BD ring in txsetup\r\n");

		return XST_FAILURE;
	}

	/*
	 * We create an all-zero BD as the template.
	 */
	XAxiDma_BdClear(&BdTemplate);

	Status = XAxiDma_BdRingClone(TxRingPtr, &BdTemplate);
	if (Status != XST_SUCCESS) {
		xil_printf("failed bdring clone in txsetup %d\r\n", Status);

		return XST_FAILURE;
	}

	/* Start the TX channel */
	Status = XAxiDma_BdRingStart(TxRingPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("failed start bdring txsetup %d\r\n", Status);

		return XST_FAILURE;
	}

	return XST_SUCCESS;
}

static int SendPacket(XAxiDma * AxiDmaInstPtr, u32 * TxArray)
{
	XAxiDma_BdRing *TxRingPtr;
	XAxiDma_Bd *BdPtr;
	int Status;

	TxRingPtr = XAxiDma_GetTxRing(AxiDmaInstPtr);

	/* Flush the buffers before the DMA transfer, in case the Data Cache
	 * is enabled
	 */
	Xil_DCacheFlushRange((UINTPTR)TxArray, MAX_PKT_LEN);

	/* Allocate a BD */
	Status = XAxiDma_BdRingAlloc(TxRingPtr, 1, &BdPtr);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	/* Set up the BD using the information of the packet to transmit */
	Status = XAxiDma_BdSetBufAddr(BdPtr, (UINTPTR) TxArray);
	if (Status != XST_SUCCESS) {
		xil_printf("Tx set buffer addr %x on BD %x failed %d\r\n",
		    (UINTPTR)TxArray, (UINTPTR)BdPtr, Status);

		return XST_FAILURE;
	}

	Status = XAxiDma_BdSetLength(BdPtr, MAX_PKT_LEN * sizeof(u32),
				TxRingPtr->MaxTransferLen);
	if (Status != XST_SUCCESS) {
		xil_printf("Tx set length %d on BD %x failed %d\r\n",
		    MAX_PKT_LEN, (UINTPTR)BdPtr, Status);

		return XST_FAILURE;
	}

	/* For single packet, both SOF and EOF are to be set
	 */
	XAxiDma_BdSetCtrl(BdPtr, XAXIDMA_BD_CTRL_TXEOF_MASK |
						XAXIDMA_BD_CTRL_TXSOF_MASK);

	XAxiDma_BdSetId(BdPtr, (UINTPTR)TxArray);

	Xil_DCacheFlushRange((UINTPTR)BdPtr, BD_SPACE_SIZE); 

	/* Give the BD to DMA to kick off the transmission. */
	Status = XAxiDma_BdRingToHw(TxRingPtr, 1, BdPtr);
	if (Status != XST_SUCCESS) {
		xil_printf("to hw failed %d\r\n", Status);
		return XST_FAILURE;
	}



	return XST_SUCCESS;
}
