int dummy(__global char* str) {
  int foo = 9, bar = 8;
  while (foo >= 0)
    while (foo < 0)
      foo = (foo ^ (bar = foo - 1)) + 1;
  return str[foo] = bar;
}

__kernel void hello(__global char *string) {
  if (get_global_id(0) != 0)
    return;
  
    do {} while (0);

    int cyka = 0;
  
    cyka = 0;
  
    string[0] = 'H';
    string[1] = 'e' + cyka;
    string[2] = 'l';
    string[3] = 'l';
    string[4] = 'o';
    string[5] = ',';
    string[6] = ' ';
    string[7] = 'W';
    string[8] = 'o';
    string[9] = 'r';
    string[10] = ':';
    string[11] = '\0';
    dummy(string);
    string[10] = 'l';
    string[11] = 'd';
    string[12] = '!';
    string[13] = ({
      char ret = '\0';
      if (0 > 1)
      
        ret += 0;
      else
        ret += 0;
    EXIT2:
      ret;
    });

    ({
      if (0 > 1)
        ;
    EXIT3:;
    });

}
