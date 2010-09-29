#include <glib.h>
#include <string.h>
#include <stdio.h>
#include "cmdparser.h"
#include "cmd.h"

GScanner* scanner;
#define SYMBOL(s,x) (s->token ==  G_TOKEN_SYMBOL && s->value.v_int == x)

void cmdparser_init(void)
{
     scanner = g_scanner_new(NULL);
     g_scanner_set_scope(scanner, 0);
     g_scanner_scope_add_symbol(scanner, 0, "set", GINT_TO_POINTER(1));
     g_scanner_scope_add_symbol(scanner, 0, "name", GINT_TO_POINTER(2));
     g_scanner_scope_add_symbol(scanner, 0, "list", GINT_TO_POINTER(3));
     g_scanner_scope_add_symbol(scanner, 0, "group", GINT_TO_POINTER(4));
     g_scanner_scope_add_symbol(scanner, 0, "add", GINT_TO_POINTER(5));
     g_scanner_scope_add_symbol(scanner, 0, "groups", GINT_TO_POINTER(6));
     g_scanner_scope_add_symbol(scanner, 0, "nodes", GINT_TO_POINTER(7));
     g_scanner_scope_add_symbol(scanner, 0, "exit", GINT_TO_POINTER(8));
}

gssize cmdparser_cmd(gchar* cmd, gsize n, gchar** result)
{
    g_scanner_input_text(scanner, cmd, n);
    g_scanner_get_next_token(scanner);
    *result = NULL;
    if( SYMBOL(scanner,3) ){ //list
        g_scanner_get_next_token(scanner);
        if( SYMBOL(scanner,7) ) //nodes
            *result = cmd_list_nodes();
        else if( SYMBOL(scanner,6) ) //groups
            *result = cmd_list_groups();
        /*g_scanner_get_next_token(scanner);
        printf("token=%d\n",scanner->token);
        if( scanner->token == G_TOKEN_STRING ||
            scanner->token == G_TOKEN_IDENTIFIER){
            printf("pong %s\n", scanner->value.v_string);
        }else{
            int value = scanner->value.v_int;
            printf("pong %d\n", value);
        }*/
    }else if( SYMBOL(scanner,8) ){ //exit
        *result = malloc(30);
        sprintf(*result,"bye\n");
        return -1;
    }
    if( *result == NULL ){
        *result = malloc(30);
        sprintf(*result,"unknown command\n");
    }
    strcat(*result,">");
    return strlen(*result);
}

