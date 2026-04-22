#include <stdio.h>
//#include <stdint.h>
//#include <stdbool.h>
#include "parameters.h"
#include "uart1.h"

tci_parameters param;

POCSAG_route route;  // definuje routu pro vysilani


void Parameters_Init(void) {
	unsigned char n;

	param.primary_net = 15;
	param.next_time = 5;
	param.next_rpt = 2;
	param.error_rpt = 2;
	param.pretime = 0;
	param.deadtime = 0;
	param.sys_tok = 0;

	for (n=0; n<MAX_NETS; n++) {
		param.netdau[n] = 0;
	}

	for (n=0; n<MAX_ROUTES; n++) {
		param.route[n].net = 0;
		param.route[n].path = 0;
		param.route[n].dau = 0;
		param.route[n].follow = 0;
		param.route[n].error = 0;
		param.route[n].revers = 0;
	}

	//---- Default pro ladeni
	param.netdau[14] = 3;
	param.route[0].net = 15;
	param.route[0].path = 255;
	param.route[0].dau = 255;
	param.route[0].follow = 2;
	param.route[0].error = 2;
	param.route[0].revers = 2;
}

void Parameters_Show(void) {
	unsigned char n;
	char txt[250] = "";

	sendStringUART1("\r\nParameters:\r\n");
	sprintf(txt,"PRIMARY NET: %u\r\n",param.primary_net);
	sendStringUART1(txt);
	sprintf(txt,"NEXT TIME: %u\r\n",param.next_time);
	sendStringUART1(txt);
	sprintf(txt,"NEXT RPT : %u\r\n",param.next_rpt);
	sendStringUART1(txt);
	sprintf(txt,"ERROR RPT: %u\r\n",param.error_rpt);
	sendStringUART1(txt);
	sprintf(txt,"PRETIME  : %u\r\n",param.pretime);
	sendStringUART1(txt);
	sprintf(txt,"DEADTIME : %u\r\n",param.deadtime);
	sendStringUART1(txt);
	sprintf(txt,"SYS.TOK  : %u\r\n",param.sys_tok);
	sendStringUART1(txt);

	sendStringUART1("-------------------------------------------------\r\nNET:");
	for (n=0; n<MAX_NETS; n++) {
		sprintf(txt," %02u",n+1);
		sendStringUART1(txt);
	}
	sendStringUART1("\r\nDAU:");
	for (n=0; n<MAX_NETS; n++) {
		if (param.netdau[n]!=0) {
			sprintf(txt," %02u",param.netdau[n]);
			sendStringUART1(txt);
		}
		else {
			sendStringUART1(" x ");
		}
	}
	sendStringUART1("\r\n-------------------------------------------------");

	sendStringUART1("\r\nROUTE: NET PTH DAU -> FLW ERR REV\r\n");
//	for (n=0; n<MAX_ROUTES; n++) {
	n=0;
	while ((n<MAX_ROUTES)&&(param.route[n].path!=0)) {
		sprintf(txt,"       %02u",param.route[n].net);
		sendStringUART1(txt);
		if (param.route[n].path==255) {
			sendStringUART1("  * ");
		}
		else {
			sprintf(txt,"  %02u",param.route[n].path);
			sendStringUART1(txt);
		}
		if (param.route[n].dau==255) {
			sendStringUART1("  * ");
		}
		else {
			sprintf(txt,"  %02u",param.route[n].dau);
			sendStringUART1(txt);
		}
		sprintf(txt,"  -> %02u",param.route[n].follow);
		sendStringUART1(txt);
		sprintf(txt,"  %02u",param.route[n].error);
		sendStringUART1(txt);
		sprintf(txt,"  %02u\r\n",param.route[n].revers);
		sendStringUART1(txt);
		n++;
	}
//	sendStringUART1("       -------------------------\r\n");
	sendStringUART1("-------------------------------------------------\r\n");
}

//------------------------------------------------------------------------------
// Podle parametru NET,PATH,DAU nacte z route table a nastavi promenou route
//------------------------------------------------------------------------------
void make_route(unsigned char net, unsigned char path, unsigned char dau)
{
	route.follow = 5;
	route.error  = 2;
	route.revers = 2;
}
