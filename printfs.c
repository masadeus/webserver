// per a comprovar HTTP/1.1

            
    // security check.
    // printf("''%c%c''    \n" ,target_init[0], target_init[1]); // target_init has correct value
            
            
    // for (int i = 0; i <= strlen(line); i++) // line is correct i.e. GET /cat.jpg HTTP/1.1
    //     printf("%c", line[i]);
    
    printf("%s line_ct", line_ct); // is cool
    
    // printf("%s", needle_1_pt); //is null

    printf("\n");
    
    printf("%i", strlen(needle_1));
    
    printf("\n");
    
    printf("%c,nnnnnnn", needle_1[7]);
    
  ///////////  
// TODO: extract query from request-target // this is the stuff after a question mark
            
            // Allocated on the stack. The problem is that is not returning '\0'
            // if there is a ? but nothing follows, like when ? is not there.
            
            char* query_bg = strchr(abs_path, '?'); // beginning query
            char* query_end = strchr(abs_path, '\0');
            int query_ln = query_end - query_bg;
            char query[query_ln]; // would look beter if these were in the if condiction, but then I lose the scope
                                  // the only option is to dinamically resize
                                  
            if (query_bg != NULL)
            {
                memset(query, 0, query_ln);
                strcpy(query, query_bg + 1); // si no funciona provar strncpy
                query[query_ln - 1] = '\0'; // append NULL in the end
           
                printf("\033[36m"); // prints in yellow
                printf("%s\n", query);
                printf("\033[39m\n");
            }
