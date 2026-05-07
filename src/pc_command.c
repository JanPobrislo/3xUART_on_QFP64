//------------------------------------------------------------------------------
//  Komunikace s PC na COM-A (UART0,9600)
//  Spolupracuje s programy: Master + TCImulti (nastaveni parametru)
//------------------------------------------------------------------------------
#include <stdio.h>
#include "em_usart.h"
#include "uart0.h"
#include "parameters.h"


typedef enum {
	IDLE,
	QUESTION,
	PC,
	PASSWORD,
	CMD
} type_pc_status;

type_pc_status pc_status = IDLE;

//------------------------------------------------------------------------------
// Simulace cteni EEPROM puvodni TCI - vypise 128 byte na COM-A (UART0)
//------------------------------------------------------------------------------
void eeprom_read_all(void) {
    char buf[300];
	unsigned char n;

    sendStringUART0("051,204,000,011,");

	sprintf(buf,"%03u,%03u,%03u,%03u,",
			param.primary_net,
			param.netdau[param.primary_net],
			param.next_time,
			param.deadtime
	);
	sendStringUART0(buf);

	sprintf(buf,"000,%03u,%03u,108,",
			param.sys_tok,
			param.next_time
	);
	sendStringUART0(buf);

	sprintf(buf,"%03u,%03u,000,000,",
			param.next_rpt,
			param.error_rpt
	);
	sendStringUART0(buf);

    sendStringUART0("001,"); //-- Output enabled

    sendStringUART0("000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,");

    for (n=0; n<MAX_NETS; n++) {
		sprintf(buf,"%03u,",param.netdau[n]);
		sendStringUART0(buf);
	}

	sendStringUART0("000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,");

//
//sendStringUART0("051,204,000,013,015,000,000,000,000,000,003,008,001,001,000,000,001,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,011,012,013,014,000,016,017,018,019,020,021,022,023,024,002,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,000,");
}

//------------------------------------------------------------------------------
// Simulace cteni EEPROM puvodni TCI - vypise routovaci tabulku na COM-A (UART0)
//------------------------------------------------------------------------------
void eeprom_read_routes(void) {
    char buf[300];
	unsigned char n;

	for (n=0; n<MAX_ROUTES; n++) {
		if (param.route[n].net == 0) break;

		sprintf(buf,"%03u,%03u,%03u,%03u,%03u,%03u,",
					param.route[n].net,
					param.route[n].path,
					param.route[n].dau,
					param.route[n].follow,
					param.route[n].error,
					param.route[n].revers
					);
		sendStringUART0(buf);
	}

}

//------------------------------------------------------------------------------
// Automat zpracuje jeden znak z COM-A
//------------------------------------------------------------------------------
void read_pc_byte(unsigned char zn) {

	char     passwd[] = "WASER";
	static unsigned char index = 0;

/*
	sendStringUART0("[");
    USART_Tx(UART0, zn);
    sendStringUART0("]");
*/
    switch (pc_status) {
	case IDLE:
		if (zn == '?') {
			pc_status = QUESTION;
		}
		break;

	//------------- Nacetl ?
	case QUESTION:
		switch(zn) {

		case '0':
			sendStringUART0("Yes");
			pc_status = IDLE;
			break;

		case '6':
			sendStringUART0("NET/DAU");  //-- ToDo: spravny format
			pc_status = IDLE;
			break;

		case 'P':
			pc_status = PC;
			break;

		default:
			pc_status = IDLE;
			break;
		}
		break;
	//------------- Nacetl ?P
	case PC:
		if (zn == 'C') {
			sendStringUART0("\r\nMULTI TCI Board Setting mode\r\nPASSWORD: ");
			pc_status = PASSWORD;
			index = 0;
		}
		else {pc_status = IDLE;}
		break;

		//------------- Nacetl ?PC
	case PASSWORD:
		if (zn == passwd[index]) {
			index++;
			if (index==5) { //-- nacetl cele heslo
				sendStringUART0("CMD> ");
				pc_status = CMD;
			}
		}
		else {
			sendStringUART0("ERROR");
			pc_status = IDLE;
		}
		break;

		//------------- Nacetl ?PC+heslo
	case CMD:
		switch(zn) {

		case 'e':
		    eeprom_read_all();
			pc_status = CMD;
			break;

		case 'p':
			eeprom_read_routes();
			pc_status = CMD;
			break;

		case 'z':
			sendStringUART0("[");
		    USART_Tx(UART0, zn);
		    sendStringUART0("]");
			pc_status = CMD;
			break;

		case 'y':
			sendStringUART0("[");
		    USART_Tx(UART0, zn);
		    sendStringUART0("]");
			pc_status = CMD;
			break;

		case '!':
			sendStringUART0(" RESET ");
			pc_status = IDLE;
			break;

		default:
			sendStringUART0("[");
		    USART_Tx(UART0, zn);
		    sendStringUART0("]");
			sendStringUART0(" UNKNOWN-CMD ");
			break;
		}

		sendStringUART0("\r\nCMD> ");
		break;

	default:
		pc_status = IDLE;
		break;
    }
}
