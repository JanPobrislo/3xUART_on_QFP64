//------------------------------------------------------------------------------
//  Komunikace s PC na COM-A (UART0,9600)
//  Spolupracuje s programy: Master + TCImulti (nastaveni parametru)
//------------------------------------------------------------------------------
#include <stdio.h>
#include "em_usart.h"
#include "uart0.h"
#include "parameters.h"
#include "uart1.h"
#include "pocsag.h"
#include "led.h"

unsigned char eeprom[128]; // pro simulaci EEPROM puvodni TCI

typedef enum {
	IDLE,
	QUESTION,
	PC,
	PASSWORD,
	CMD,
	CMD_Z,
	CMD_Y,
	MASTER_TOKEN_BATCH,
	MASTER_TOKEN_PATH,
	MASTER_TOKEN_TIMEOUT,
	MASTER_TOKEN_ID,
	MASTER_TOKEN_TYP,
	MASTER_TOKEN_DATA
} type_pc_status;

type_pc_status pc_state = IDLE;

POCSAG_token master_token; // token prijaty z PC

//------------------------------------------------------------------------------
//  SW vyvolani resetu.
//------------------------------------------------------------------------------
void perform_system_reset(void) {
    // Provede softwarový reset celého procesoru
    // Používá CMSIS funkci, která zapíše do registru AIRCR
    // specifický "klíè" a bit SYSRESETREQ
    NVIC_SystemReset();
}

//------------------------------------------------------------------------------
// Smaze pole eeprom
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
			param.netdau[param.primary_net-1],
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
	unsigned char n;

    if (eeprom[0]!=51 || eeprom[1]!=204) return;

    sendStringUART1("\r\nWRITE EEPROM:[");

	param.primary_net = eeprom[4];
//	param.netdau[param.primary_net-1] = eeprom[5];
	param.pretime = eeprom[6];
	param.deadtime = eeprom[7];

	param.sys_tok = eeprom[9];
	param.next_time = eeprom[10];
	param.next_rpt = eeprom[12];
	param.error_rpt = eeprom[13];
	param.out_enabled = eeprom[16];

	parameters_clear_netdau();
	for (n=0; n<MAX_NETS; n++) {
		param.netdau[n] = eeprom[41+n];
	}

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
    char buf[200];
	char passwd[] = "WASER";
	static unsigned int index = 0;
	static uint32_t word = 0;
	unsigned char w,b; // pocitadla slov a bitu

/*
	sendStringUART0("[");
    USART_Tx(UART0, zn);
    sendStringUART0("]");
*/
	if (master_state == MASTER_IDLE || master_state == MASTER_CONFIRMED)
		{LED3_On();}

    switch (pc_state) {
	case IDLE:
		if (zn == '?') {
			pc_state = QUESTION;
		}
		break;

	//------------- Nacetl ?
	case QUESTION:
		switch(zn) {

		//-- Pravidelny dotaz
		case '0':
			sendStringUART0("Yes");
			pc_state = IDLE;
			break;

		//-- Dotaz na potvrzeni obehu tokenu
		case '2':
		    switch (master_state) {
				case MASTER_IDLE:  //-- to by nemelo nastat
					sendStringUART0("ResetTCI");
					break;
				case MASTER_LOADING:  //-- to by nemelo nastat
					sendStringUART0("ResetTCI");
					break;
		        case MASTER_PREPARED:
					sendStringUART0("Wait");
		            break;
		        case MASTER_SENT:
					sendStringUART0("Wait");
		            break;
		        case MASTER_RETURNED:
					sendStringUART0("Tok");
					USART_Tx(UART0,master_token.path);
					USART_Tx(UART0,master_token.pass_dau);
					master_state = MASTER_CONFIRMED;
		            break;
		        case MASTER_CONFIRMED:
					sendStringUART0("Tok");
					USART_Tx(UART0,master_token.path);
					USART_Tx(UART0,master_token.pass_dau);
		            break;
		    }
			break;

		//-- Nacte master token z PC
		case '3':
			sendStringUART0("Ready");
			master_state = MASTER_LOADING;
			pc_state = MASTER_TOKEN_BATCH;
			break;

		//-- Odesle token do PC
		case '4':
			if (master_state == MASTER_CONFIRMED) {
				sprintf(buf,"Token:%u",master_token.batch);
				sendStringUART0(buf);
				for (w=0; w < master_token.total_words; w++) {
					for (b=0; b<21; b++) {
						if ((master_token.data[w] & (uint32_t)(0x80000000>>b)) == 0)
							 {sendStringUART0("0");}
						else {sendStringUART0("1");}
					}
					sendStringUART0("\r");
				}
				sprintf(buf,"E%cF",master_token.total_words);
				sendStringUART0(buf);
			}
			break;

		//-- Dotaz na NET/DAU
		case '6':
			sprintf(buf,"%c%c",param.primary_net,param.netdau[param.primary_net-1]);
			sendStringUART0(buf);
			pc_state = IDLE;
			break;

		//-- Pripojeni PC - TCImulti
		case 'P':
			pc_state = PC;
			break;

		default:
			pc_state = IDLE;
			break;
		}
		break;

	//------------- Nacetl ?3
	case MASTER_TOKEN_BATCH:
		master_token.batch = zn;
		master_token.total_words = zn*WORDS_PER_BATCH;
		pc_state = MASTER_TOKEN_PATH;
		break;
	case MASTER_TOKEN_PATH:
		master_token.path = zn;
		pc_state = MASTER_TOKEN_TIMEOUT;
		break;
	case MASTER_TOKEN_TIMEOUT:
		master_token.timeout = zn;
		pc_state = MASTER_TOKEN_ID;
		break;
	case MASTER_TOKEN_ID:
		master_token.token_id = zn;
		pc_state = MASTER_TOKEN_TYP;
		break;
	case MASTER_TOKEN_TYP:
		master_token.token_type = zn;
		index = 0;
		word = 0;
		pc_state = MASTER_TOKEN_DATA;
//	    sendStringUART1("MASTER TOKEN: HEAD OK\r\n");
		break;

	case MASTER_TOKEN_DATA:
		if (zn=='E') {
			if (index == master_token.batch*WORDS_PER_BATCH*21) {
				sendStringUART0("OK");
				sendStringUART1("\r\n--- MASTER TOKEN ---\r\n");
			    master_state = MASTER_PREPARED;
			    LED3_On();
			}
			else {
				sendStringUART0("ERROR");
//			    sendStringUART1("ERROR: Token NOT loaded from the master\r\n");
//				sprintf(buf,"TOKEN=%u BATCH=%u MASTER=%02u ",rx_token.token_id,rx_token.batch,rx_token.master);
				sprintf(buf,"\r\nERROR: Token NOT loaded from the master [INDEX=%u]\r\n ",index);
			    sendStringUART1(buf);
			}
			pc_state = IDLE;
		}
		else {
			index++;
			word <<= 1;
			if (zn=='1') {word++;}

			if (index%21 == 0) { // prijat jeden cely CDW
				word <<= 11;
				master_token.data[(index/21)-1] = word;
				word = 0;
			}
		}
		if (index > MAX_BATCHES*WORDS_PER_BATCH*21) {
			//-- jen pro jistotu, aby nepretekl
		    sendStringUART1("\r\nERROR: Token NOT loaded from the master!\r\n");
		    pc_state = IDLE;
		}
		break;

	//------------- Nacetl ?P
	case PC:
		if (zn == 'C') {
			sendStringUART0("\r\nMULTI TCI Board Setting mode\r\nPASSWORD: ");
			pc_state = PASSWORD;
			index = 0;
		}
		else {pc_state = IDLE;}
		break;

		//------------- Nacetl ?PC
	case PASSWORD:
		if (zn == passwd[index]) {
			index++;
			if (index==5) { //-- nacetl cele heslo
				sendStringUART0("CMD> ");
				pc_state = CMD;
			}
		}
		else {
			sendStringUART0("ERROR");
			pc_state = IDLE;
		}
		break;

		//------------- Nacetl ?PC+heslo
	case CMD:
		switch(zn) {

		case 'e':
		    eeprom_read_all();
			pc_state = CMD;
			break;

		case 'p':
			eeprom_read_routes();
			pc_state = CMD;
			break;

		case 'z':
			sendStringUART0(":");
			index = 0;
			eeprom_clear_all();
			pc_state = CMD_Z;
			break;

		case 'y':
			sendStringUART0("\n\rWRITE ROUTE TABLE:  (128=END\n\r");
			index = 0;
			eeprom_clear_all();
			pc_state = CMD_Y;
			break;

		case '!':
			sendStringUART0(" RESET ");
			pc_state = IDLE;
			perform_system_reset();
			break;

		case '?':
			pc_state = QUESTION;
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
			//-- nezapisuje do flash, protoze z TCImulti nasleduje prikaz 'y'
			pc_state = CMD;
		}
		break;

	case CMD_Y:
		if (zn==128 || index > (MAX_ROUTES*6)+2) { //-- 128 ukoncuje zapis EEPROM
			eeprom[index]=zn; // zapise i znak 128
			sendStringUART0("\r\nROUTE TABLE WRITED\r\nCMD> ");
			eeprom_show_all();
			eeprom_write_routes(index/6);
			parameters_save();  //-- zapise do flash
			pc_state = CMD;
		}
		else {
			eeprom[index]=zn;
			index++;
		}
		break;

	default:
		pc_state = IDLE;
		break;
    }
	if (master_state == MASTER_IDLE || master_state == MASTER_CONFIRMED)
		{LED3_Off();}
}
