//------------------------------------------------------------------------------
//  Komunikace s PC na COM-A (UART0,9600)
//  Spolupracuje s programy: Master + TCImulti (nastaveni parametru)
//------------------------------------------------------------------------------
#include <stdio.h>
#include "em_usart.h"
#include "uart0.h"
#include "parameters.h"
#include "uart1.h"

unsigned char eeprom[128]; // pro simulaci EEPROM puvodni TCI


typedef enum {
	IDLE,
	QUESTION,
	PC,
	PASSWORD,
	CMD,
	CMD_Z,
	CMD_Y
} type_pc_status;

type_pc_status pc_status = IDLE;

//------------------------------------------------------------------------------
// Simulace zapisu do EEPROM puvodni TCI - smaze pole eeprom
//------------------------------------------------------------------------------
void eeprom_clear_all(void) {
	unsigned char n;
	for (n=0; n<128; n++) {
		eeprom[n]=0;
	}
}

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
			param.pretime,
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

	sendStringUART0("p\r\n");

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
    sendStringUART0("\r\n");
}

//------------------------------------------------------------------------------
// Vypise eeprom[] na UART1
//------------------------------------------------------------------------------
void eeprom_show_all(void) {
    char buf[300];
	unsigned char n;

    sendStringUART1("\r\nEEPROM:[");

	for (n=0; n<128; n++) {
		sprintf(buf,"%03u,",eeprom[n]);
		sendStringUART1(buf);

	}
    sendStringUART1("]\r\n");
}


//------------------------------------------------------------------------------
// Simulace zapisu do EEPROM puvodni TCI - prijme 128 byte z COM-A (UART0)
//------------------------------------------------------------------------------
void eeprom_write_all(void) {
//    char buf[300];
//	unsigned char n;

    if (eeprom[0]!=51 || eeprom[1]!=204) return;

    sendStringUART1("\r\nWRITE EEPROM:[");

	param.primary_net = eeprom[4];
//	param.netdau[param.primary_net] = eeprom[5];
	param.pretime = eeprom[6];
	param.deadtime = eeprom[7];

	param.sys_tok = eeprom[9];
	param.next_time = eeprom[10];
	param.next_rpt = eeprom[12];
	param.error_rpt = eeprom[13];

    sendStringUART1("]\r\n");
}

//------------------------------------------------------------------------------
// Simulace zapisu do EEPROM puvodni TCI - zapise routovaci tabulku z COM-A
//------------------------------------------------------------------------------
void eeprom_write_routes(unsigned char routes) {
    char buf[300];
	unsigned char n;

    if (routes == 0 || routes>MAX_ROUTES) return;

    sendStringUART1("\r\nWRITE ROUTES: [");
	sprintf(buf,"%ux]\r\n",routes);
	sendStringUART1(buf);

	parameters_clear_routes();

	for (n=0; n<routes; n++) {
		param.route[n].net    = eeprom[((n)*6)];
		param.route[n].path   = eeprom[((n)*6)+1];
		param.route[n].dau    = eeprom[((n)*6)+2];
		param.route[n].follow = eeprom[((n)*6)+3];
		param.route[n].error  = eeprom[((n)*6)+4];
		param.route[n].revers = eeprom[((n)*6)+5];
	}
    sendStringUART1("END\r\n");
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
			sendStringUART0(":");
			index = 0;
			eeprom_clear_all();
			pc_status = CMD_Z;
			break;

		case 'y':
			sendStringUART0("\n\rWRITE ROUTE TABLE:  (128=END\n\r");
			index = 0;
			eeprom_clear_all();
			pc_status = CMD_Y;
			break;

		case '!':
			sendStringUART0(" RESET ");
			pc_status = IDLE;
			break;

		case '?':
			pc_status = QUESTION;
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

	case CMD_Z:
		if (index<128) { //-- cte 128 byte EEPROM
			eeprom[index]=zn;
			index++;
		}
		else {
			eeprom_show_all();
			eeprom_write_all();
			sendStringUART0("\r\nCMD> ");
			pc_status = CMD;
		}
		break;

	case CMD_Y:
		if (zn==128 || index > (MAX_ROUTES*6)+2) { //-- 128 ukoncuje zapis EEPROM
			eeprom[index]=zn; // zapise i znak 128
			sendStringUART0("\r\nROUTE TABLE WRITED\r\nCMD> ");
			eeprom_show_all();
			eeprom_write_routes(index/6);
			pc_status = CMD;
		}
		else {
			eeprom[index]=zn;
			index++;
		}
		break;

	default:
		pc_status = IDLE;
		break;
    }
}
