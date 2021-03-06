#include "uHTRPowerMezzMenu.h"
#include <curses.h>
#include "io.h"

extern std::string parseto(std::string &buff, std::string del = ",")
{
    size_t pos = buff.find(del);
    if(pos == std::string::npos) pos = buff.size();
    std::string ret = buff.substr(0, pos);
    pos = buff.find_first_not_of(del + " \t", pos);
    buff.erase(0, pos);
    return ret;
}

uHTRPowerMezzMenu::uHTRPowerMezzMenu(std::map< int, std::string> config_lines, bool isV2,const char tstr[])
    : yinit_(0),xinit_(0)
{
    //Handle signals
    
    struct sigaction sa;

    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; /* Restart functions if
                                 interrupted by handler */
    sprintf(tester,"%s",tstr);

    for (std::map< int, std::string>::iterator it = config_lines.begin();
            it!= config_lines.end();
            it++)
    {
        Board b;
        b.id = it->first;
        std::string config_line = it->second;

        b.adChan = atoi(parseto(config_line).c_str());
        sprintf(b.adapter,  "%s", parseto(config_line).c_str());
        sprintf(b.hostname, "%s", parseto(config_line,":").c_str());
        sprintf(b.port,     "%s", parseto(config_line).c_str());

        // Initialize i2c device
        bool isRPi = false;
        if(strcmp(b.adapter, "RPi") == 0) isRPi = true;
        else if(strcmp(b.adapter, "S20") == 0) isRPi = false;
        else 
        {
            printf("i2c adapter: %s is invalid!!! (Options are \"RPi\" and \"S20\")\n", b.adapter);
        }
        b.s20 = new uHTRPowerMezzInterface(0, isV2, isRPi,b.hostname,b.port);
        b.s20->setMUXChannel(0,b.adChan);

        if(isRPi && (b.adChan < 1 || b.adChan > 6)) 
        {
            printf("Invalid RPi adapter channel (1 - 6 are valid)!!!\n");
            continue;
        }
        else if(!isRPi)
        {
            b.adChan = -1; // unnecessary for s20 adapter
        }

        // Grab mezzanines 
        char slot[32];
        float voltage;
        bool mezzIn[]= {false,false,false,false,false};
        b.mezzanines = new Mezzanines();
        for(std::string mezz = parseto(config_line); mezz != ""; mezz = parseto(config_line))
        {
            if(sscanf(mezz.c_str(), "%s %f\n", slot, &voltage) == 2)
            {
                b.slots.push_back(std::make_pair(std::string(slot),voltage));

                if(strcmp(slot, "PM_3_3") == 0 && !mezzIn[0])
                {
                    b.mezzanines->push_back(new PM(*b.s20, V2_MUX_PM_3_3, voltage, true, b.adChan));
                    mezzIn[0]=true;
                }
                else if(strcmp(slot, "APM_2_5") == 0 && !mezzIn[1])
                {
                    b.mezzanines->push_back(new APM(*b.s20, V2_MUX_APM_2_5, voltage, true, b.adChan));
                    mezzIn[1]=true;
                }
                else if(strcmp(slot, "PM_1_B") == 0 && !mezzIn[2])
                {
                    b.mezzanines->push_back(new PM(*b.s20, V2_MUX_PM_1_B, voltage, true, b.adChan));
                    mezzIn[2]=true;
                }
                else if(strcmp(slot, "APM_1_6") == 0 && !mezzIn[3])
                {
                    b.mezzanines->push_back(new APM(*b.s20, V2_MUX_APM_1_6, voltage, true, b.adChan));
                    mezzIn[3]=true;
                }
                else if(strcmp(slot, "PM_1_A") == 0 && !mezzIn[4])
                {
                    b.mezzanines->push_back(new PM(*b.s20, V2_MUX_PM_1_A, voltage, true, b.adChan));
                    mezzIn[4]=true;
                }
                else
                {
                    printf("Invalid or duplicate Mezzanine slot: %s (options are PM_3_3, APM_2_5, PM_1_B, APM_1_6, PM_1_A)!!!\n", slot);
                }
            }
        }
        b.pid = 0;
        b.time = 0;
        b.voltage = 0;
        boards_[b.id] = b;
    
    }
    selected_board_ = boards_.begin();
    initscr();
    if (has_colors())
    {
        start_color();
        init_pair(1, COLOR_GREEN, COLOR_BLACK );
        init_pair(2, COLOR_BLUE,  COLOR_BLACK );
        init_pair(3, COLOR_RED,   COLOR_BLACK  );
    }
    erase();
    noecho();
    nodelay(stdscr, true);
    keypad(stdscr,true);
    cbreak(); 
    curs_set(0);

    io::enable_curses(yinit_ + 7 + boards_.size(), LINES - 2 ,xinit_ + 2);

    n_checks_ = shutdown_time_;
}

void uHTRPowerMezzMenu::check_voltages()
{
    for(std::map<int,Board>::iterator board = boards_.begin();
            board != boards_.end();
            ++board)
    {
        uHTRPowerMezzInterface * s20 = board->second.s20;
        board->second.voltage = 0;

        if(s20->isV2_)
        {
            // Read voltage from sub20 board

            if(s20->isRPi_)
            {
                // 12 V supply is read through a divide by 9.2
                //
                if(9.2 * s20->readSUB20ADC(7 - 2*(board->second.adChan - 1)) < 10.0) board->second.voltage |= RETVAL_BAD_SUPPLY_VOLTAGE;

                // 5 V supply is read through a dividr by 3.7
                if(3.7 * s20->readSUB20ADC(6 - 2*(board->second.adChan - 1)) < 4.5) board->second.voltage |= RETVAL_BAD_3_3VOLT;

            }
            else
            {
                // 12 V supply is read through a divide by 9.2
                if(9.2 * s20->readSUB20ADC(6) < 10.0) board->second.voltage |= RETVAL_BAD_SUPPLY_VOLTAGE;

                // 5 V supply is read through a dividr by 3.7
                if(3.7 * s20->readSUB20ADC(7) < 4.5) board->second.voltage |= RETVAL_BAD_3_3VOLT;
            }
        }
        else
        {
            //--------------------------------------------------------------------
            // Set MUX to channel 1, AUX_POWER MODULE
            //--------------------------------------------------------------------
            // PCA9544A; SLAVE ADDRESS: 1110 000
            s20->setMUXChannel(MUX_AUXPOWERMOD, -1);

            //--------------------------------------------------------------------
            // Read 12V supply voltage. If 12V supply reads less than 8V then 
            // exit with a failure.
            //--------------------------------------------------------------------
            // MCP3428; SLAVE ADDRESS: 1101 010
            // Channel 3: V12 = 18.647 * V_CH3
            // 2.048V reference -> 1 mV per LSB
            if(18.647 * s20->readMezzADC(ADC_28, 3) < 10000.0) board->second.voltage |= RETVAL_BAD_SUPPLY_VOLTAGE;

            //--------------------------------------------------------------------
            // Read 3.3V supply voltage. If 3.3V supply reads less than 3V then 
            // exit with a failure.
            //--------------------------------------------------------------------
            // MCP3428; SLAVE ADDRESS: 1101 010
            // Channel 4: V3.3 = 18.647 * V_CH4
            // 2.048V reference -> 1 mV per LSB
            if(18.647 * s20->readMezzADC(ADC_28, 4) < 3000.0) board->second.voltage |= RETVAL_BAD_3_3VOLT;
        }
        board->second.voltage |= RETVAL_SUCCESS;
    }
}

void uHTRPowerMezzMenu::draw_star()
{
    attrset(COLOR_PAIR(2));
    int y = 4 + yinit_;
    int x = 2 + xinit_;
    for(std::map<int,Board>::iterator board = boards_.begin();
            board != boards_.end();
            ++board)
    {
        if(selected_board_  == board) mvaddch(y,x,'*');
        else mvaddch(y,x,' ');
        ++y;
    }

}

void uHTRPowerMezzMenu::display()
{
    if ( n_checks_ <=  6*5 && !(n_checks_ % 6)) 
        io::printf("Closing Interactive in %d minutes", n_checks_/6);
    n_checks_--;

    check_voltages();

    char buff[256];
    char header1[256];
    char header2[256];
    char header3[256];
    char bars[256];
    int x = xinit_+1 , y = 4 + yinit_;



    attrset(COLOR_PAIR(1));

    sprintf(buff,"Reading...");
    mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);
    refresh();  


    // 0         1         2         3         4         5         6         7         8         9         10        11        12        13
    // 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012
    sprintf(header1, " Board |          Mezz 1        |         Mezz 2         |         Mezz 3         |         Mezz 4         |         Mezz 5         |    Time  |");
    sprintf(header2, "       |Temp(C) Volt(V) Power(W)|   Temp    Volt   Power |   Temp    Volt   Power |   Temp    Volt   Power |   Temp    Volt   Power | DD:HH:MM |");
    sprintf(header3, "-------|------------------------|------------------------|------------------------|------------------------|------------------------|----------|");
    sprintf(bars,    "       |                        |                        |                        |                        |                        |          |");
    //               "   88  |  12.34   12.34   12.34 |  12.34   12.34   12.34 |  12.34   12.34   12.34 |  12.34   12.34   12.34 |  12.34   12.34   12.34 | 12:12:12 |");
    //                       |1234567 1234567 1234567 | 
    //                       7         -25-           32                       57                       82                       107                      132 

    mvaddstr(yinit_+1,xinit_+1,header1);
    mvaddstr(yinit_+2,xinit_+1,header2);
    mvaddstr(yinit_+3,xinit_+1,header3);



    for(std::map<int,Board>::iterator board = boards_.begin();
            board != boards_.end();
            ++board)
    {
         mvaddstr(y,x, bars);

        Board * b =  &board->second;
        //printf("Board %d",b->id); 
        sprintf(buff,"   %2d",b->id);
        mvaddstr(y,x,buff);
        if(selected_board_  == board) 
        {
            attrset(COLOR_PAIR(2));
            mvaddch(y,x+1,'*');
            attrset(COLOR_PAIR(1));
        }
        x+=8;

        //check connection
        char response[32];
        bool fail = false;
        if (b->isConnected)
        {
            if(b->pid != 0)
            {
                char retmessage[32];
                Mezzanine::Summary::translateStatus(b->voltage, retmessage);

                if(b->voltage == RETVAL_SUCCESS)
                {
                    b->mezzanines->monitor(true);
                    Mezzanines::iterator iM = b->mezzanines->begin();
                    for(;iM != b->mezzanines->end(); iM++)
                    {
                        if((*iM)->isPresent())
                        {
                            attrset(COLOR_PAIR(1 + 2*(*iM)->tempCheck((*iM)->actTest->temp[4])));
                            sprintf(buff,"%7.2f", (*iM)->actTest->temp[4]);
                            mvaddstr(y,x,buff);
                            attrset(COLOR_PAIR(1 + 2*(*iM)->voutCheck((*iM)->actTest->vout[4])));
                            sprintf(buff,"%7.2f", (*iM)->actTest->vout[4]);
                            mvaddstr(y,x+8,buff);
                            attrset(COLOR_PAIR(1 + 2*(*iM)->powerCheck((*iM)->actTest->P[4])));
                            sprintf(buff,"%7.2f", (*iM)->actTest->P[4] );
                            mvaddstr(y,x+16,buff);
                        }

                        x+=25;
                    }
                    attrset(COLOR_PAIR(1));
                    int days = b->time / 60 / 60 / 24;
                    int hours = (b->time / 60 / 60) % 24;
                    int minutes = (b->time / 60) % 60;
                    sprintf(buff," %02d:%02d:%02d",days,hours,minutes);
                    //sprintf(buff,"%2d:%d", b->time,b->pid);
                    mvaddstr(y,x,buff);
                }
                else
                {
                    sprintf(response,"%s (is the board on?)", retmessage);
                    fail = true;
                    attrset(COLOR_PAIR(3));
                }
            }
            else
            {
                sprintf(response,"No active test");
                fail = true;
                attrset(COLOR_PAIR(2));
            }
        }
        else
        {
            sprintf(response,"Not Connected");
            fail = true;
            attrset(COLOR_PAIR(2));
        }

        if(fail)
        {
            mvaddstr(y,x, response);
            attrset(COLOR_PAIR(1));
        }
        y++;
        x=1+xinit_;

    }

    sprintf(buff,"           ");
    mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);
}

void uHTRPowerMezzMenu::query_server(Board * b)
{
    int ret[2];
    b->isConnected = b->s20->can_connect();
    if(b->isConnected)
    {
        b->s20->readTest(ret);
        b->pid = ret[1];
        b->time = ret[0];

    }
    else
    {
        b->pid = 0;
        b->time = 0;
    }

}
void uHTRPowerMezzMenu::query_servers()
{
    for(std::map<int,Board>::iterator board = boards_.begin();
            board != boards_.end();
            board++)
    {
        Board * b =  &board->second;
        query_server(b);
    }
}
char uHTRPowerMezzMenu::readch()
{
    echo();
    raw();
    nodelay(stdscr, false);

    char key_code = getch();

    noecho();
    cbreak();
    nodelay(stdscr, true);
    return key_code;
}

void uHTRPowerMezzMenu::readstr(char str[])
{
    echo();
    raw();
    nodelay(stdscr, false);

    //int i = 0;
    //do
    //{
    //    str[i] = getch();
    //}
    //while( (str[i] != '\n') && i++);
    //str[i] = '\0';

    getstr(str);
    noecho();
    cbreak();
    nodelay(stdscr, true);
}


int uHTRPowerMezzMenu::start_test(char *responce)
{

    if (n_checks_ < 0) return -1;
    int key_code;
    char resp;
    if(( key_code = getch()) == ERR)
    {
        return 0;
    }
    else
    {
        n_checks_ = shutdown_time_;
        *responce = key_code;
        char buff[64];
        switch(key_code)
        {
            case KEY_DOWN:
                ++selected_board_;
                if(selected_board_ == boards_.end())
                    selected_board_ = boards_.begin();
                draw_star();
                break;
            case KEY_UP:
                if(selected_board_ == boards_.begin())
                {
                    selected_board_ = boards_.end();
                    --selected_board_;
                }
                else 
                    --selected_board_;
                draw_star();
                break;
            case 'q':
                return -1;
                break;
            case 's':
            case 't':
                query_server(&selected_board_->second);
                if(selected_board_->second.pid != 0 ) break;

                sprintf(buff,"Start test on board %d [y,N]:   ",selected_board_->first);
                mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);
                resp = readch();
                sprintf(buff,"                                ");
                mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);

                if(resp != 'y') break;

                if(key_code == 's')
                {
                    sprintf(buff,"Enter name[%s]:",tester);
                    mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);

                    char tstr[64];
                    readstr(tstr);
                    sprintf(buff,"                                     ");
                    mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);
                    if(strlen(tstr) != 0) 
                    {
                        sprintf(tester,"%s",tstr);
                    }
                    else if (strlen(tester) == 0)
                    {
                        sprintf(buff,"Tester must be set to start test");
                        mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);
                        break;
                    }

                    selected_board_->second.mezzanines->labelAll(tester,"MINNESOTA");
                    usleep(100000);
                }

                if(fork() == 0) return selected_board_->first;
                break;
            case 'k':
                //Check if Process exists
                //if not ask to cleanup board
                if(selected_board_->second.pid == 0) break;
                if(kill(selected_board_->second.pid,0))
                {
                    sprintf(buff,"PID %d not found. Cleanup? [Y/n]",selected_board_->second.pid);
                    mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);
                    resp = readch();
                    sprintf(buff,"                                   ");
                    mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);
                    if(resp == KEY_ENTER || resp == 'y') 
                    {
                        //selected_board_->second.mezzanines->setPrimaryLoad(false, false);
                        //selected_board_->second.mezzanines->setSecondaryLoad(false, false, false, false);
                        //selected_board_->second.mezzanines->setRun(false);
                        selected_board_->second.s20->stopTest();
                    }
                }
                else 
                {
                    sprintf(buff,"Kill test on board %d (pid %d) [y,N]:",selected_board_->first,selected_board_->second.pid);
                    mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);
                    resp = readch();
                    sprintf(buff,"                                       ");
                    mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);
                    if(resp != 'y') break;

                    if (kill(selected_board_->second.pid,SIGINT))
                    {

                        sprintf(buff,"Failed to kill %d pid with SIGINT. Force test to stop? [y/N]", selected_board_->second.pid);
                        mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);
                        resp = readch();
                        sprintf(buff,"                                                                ");
                        mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);
                        if(resp != 'y') break;

                        if(kill(selected_board_->second.pid,SIGKILL))
                        {
                            sprintf(buff,"Failed to kill %d pid with SIGKILL. You might need help.", selected_board_->second.pid);
                            mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);
                        }
                        //selected_board_->second.mezzanines->setPrimaryLoad(false, false);
                        //selected_board_->second.mezzanines->setSecondaryLoad(false, false, false, false);
                        //selected_board_->second.mezzanines->setRun(false);
                    }
                }
                //break;
		// Purposeful flowthrough to 'd' DO NOT place anything between 'k' and 'd'
            case 'd':
                io::printf("Disabling board %d", selected_board_->first);
                selected_board_->second.mezzanines->disableMezzanines();
                break;
            case 'g':
                /*sprintf(buff,"Start test on all idle boards[y,N]:");
                mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);
                resp = readch();
                sprintf(buff,"                                ");
                mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);

                if(resp != 'y') break;

                {
                    sprintf(buff,"Enter name[%s]:",tester);
                    mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);

                    char tstr[64];
                    readstr(tstr);
                    sprintf(buff,"                                     ");
                    mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);

                    if(strlen(tstr) != 0) 
                    {
                        sprintf(tester,"%s",tstr);
                    }
                    else if (strlen(tester) == 0)
                    {
                        sprintf(buff,"Tester must be set to start test");
                        mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);
                        break;
                    }

                    query_servers();

                    for(std::map<int,Board>::iterator board = boards_.begin();
                            board != boards_.end();
                            board++)
                    {
                        if(board->second.pid != 0 || !board->second.isConnected) continue;

                        board->second.mezzanines->labelAll(tester,"Minnesota");
                        pid_t pid = fork();
                        if(pid == 0) return board->first;
                    }
		    }*/
                break;

            case 'r':
                query_server(&selected_board_->second);
                if(selected_board_->second.pid != 0 )
                {
                    selected_board_->second.mezzanines->printMezzMAC();
                }
                else
                {
                    io::set_wait(true);
                    selected_board_->second.mezzanines->readEeprom();
                    io::set_wait(false);
                }
                break;
            case 'w':
                {
                    query_server(&selected_board_->second);
                    if(selected_board_->second.pid != 0 ) break;

                    sprintf(buff,"Enter name[%s]:",tester);
                    mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);

                    char tstr[64];
                    readstr(tstr);
                    sprintf(buff,"                                     ");
                    mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);

                    if(strlen(tstr) != 0) 
                    {
                        sprintf(tester,"%s",tstr);
                    }
                    else if (strlen(tester) == 0)
                    {
                        sprintf(buff,"Tester must be set to start test");
                        mvaddstr(yinit_ + 5 + boards_.size(),xinit_ + 2,buff);
                        break;
                    }

                    selected_board_->second.mezzanines->labelAll(tester,"Minnesota");
                }
                break;

            case 'p':
                break;

            case '?':
                io::set_wait(true);

                io::printf("**************Controls*****************");
                io::printf("'Up/Down' - Change selected board");
                io::printf("'s' - Start Short Test on selected board");
                io::printf("'t' - Start Long Test on selected board");
                io::printf("'k' - Kill Test on selected board");
                io::printf("'d' - Disable selected board WITHOUT ASKING ANY QUESTIONS!");
                io::printf("'r' - Read EEPROM on selected board");
                io::printf("'w' - Write EEPROM on selected board");
                io::printf("'p' - Print Mac Addresses");
                //io::printf("'p' - Program EEPROM on all idle boards");
                //io::printf("'g' - Start all idle boards (Go)");
                io::printf("'q' - Quit");

                io::set_wait(false);

            default:
                break;
        }
        return 0;
    }
}

void uHTRPowerMezzMenu::quit()
{
    endwin();
    exit(0);
}
