#ifdef _WIN32
#  include <windows.h>
#endif

#include <stdio.h>

#ifndef _WIN32
#  include <signal.h>
#  include <sys/time.h>
#endif

#include <boost/filesystem/operations.hpp>
#include <boost/algorithm/string.hpp>
namespace fs=boost::filesystem;
#include "solution_tree.h"
#include "solution_tree_fixes.h"
#include "bin_index.h"
#include <stdexcept>
#include "../algo/wsplayer.h"
#include "../algo/wsplayer_node.h"
#include "../extern/object_progress.hpp"
#include "../algo/env_variables.h"
#include "bin_index_solution_base.h"
#include "mysql_solution_base.h"

using namespace Gomoku;

ObjectProgress::logout_cerr log_err;
ObjectProgress::logout_file log_file;

void print_use()
{
	printf("USE: \n");
	printf("db <root_dir> get_job\n");
	printf("db <root_dir> get_ant_job [root_key]\n");
	printf("db <root_dir> save_job <file_name>\n");
	printf("db <root_dir> get <key>\n");
	printf("db <root_dir> view <printable_steps>\n");
	printf("db <root_dir> view_hex <printable_steps>\n");
	printf("db <root_dir> view_root\n");
	printf("db <root_dir> solve_level [iteration_count]\n");
	printf("db <root_dir> solve_ant [root_key] [iteration_count]\n");
	printf("db <root_dir> fix_zero_fails\n");
	printf("db <root_dir> relax <key>\n");
    Gomoku::print_enviropment_variables_hint();
	printf("win_neitrals (default: %u)\n",solution_tree_t::win_neitrals);
	printf("mysql_db  (no default)\n");
}

void self_solve(solution_tree_t& tr,const steps_t& key)
{
	steps_t init_state=key;
	Step last_step=last_color(init_state.size());

	steps_t::iterator i=init_state.begin();
	
	for(;i!=init_state.end();++i)
		if(i->step==last_step)
			break;

	if(i==init_state.end())
		throw std::runtime_error("self_solve(): state invalid");
	std::swap(*i,*(init_state.end()-1));

	game_t gm;
	gm.field().set_steps(init_state);

	WsPlayer::wsplayer_t pl;

	pl.init(gm,other_color(last_step));
	pl.solve();

	const WsPlayer::wide_item_t& r=static_cast<const WsPlayer::wide_item_t&>(*pl.root);

	points_t neitrals;
	items2points(r.get_neitrals(),neitrals);

	npoints_t wins;
	items2depth_npoints(r.get_wins().get_vals(),wins);

	npoints_t fails;
	items2depth_npoints(r.get_fails().get_vals(),fails);

	tr.trunc_neitrals(key,neitrals,wins,fails);

	std::string str_key=print_steps(key);
	ObjectProgress::log_generator lg(true);
	lg<<str_key<<": n="<<neitrals.size()<<" w="<<wins.size()<<" f="<<fails.size();

	tr.save_job(key,neitrals,wins,fails);
}

static bool need_break;

#ifdef _WIN32
BOOL WINAPI CtrlCHandlerRoutine(DWORD dwCtrlType)
{
	if(dwCtrlType!=CTRL_C_EVENT)return FALSE;
	need_break=true;
	return TRUE;
}
#else
void sig_handler(int v)
{
	need_break=true;
}
#endif

struct fix_zero_deep_fails_impls : public fix_zero_deep_fails
{
    fix_zero_deep_fails_impls(solution_tree_t& _tree) : fix_zero_deep_fails(_tree) {}
    virtual bool is_canceled(){return need_break;}
};

void set_ctrl_handler()
{
	need_break=false;
#ifdef _WIN32
	SetConsoleCtrlHandler(CtrlCHandlerRoutine,TRUE);
#else
  signal(SIGINT,sig_handler);
	signal(SIGTSTP,sig_handler);
#endif
}

void self_solve(solution_tree_t& tr,size_t iteration_count,const steps_t& root_key=steps_t(),bool use_ant=false)
{
    scan_enviropment_variables();

	const char* sval=getenv("win_neitrals");
	if(sval!=0&&(*sval)!=0)
		solution_tree_t::win_neitrals=atol(sval);

	ObjectProgress::log_generator lg(true);
	print_used_enviropment_variables(lg);
    lg<<"win_neitrals="<<solution_tree_t::win_neitrals;

    set_ctrl_handler();

	size_t key_len=0;

	for(size_t i=0;;i++)
	{
		if(need_break)
		{
			lg<<"Ctrl-C pressed. Exit";
			printf("Ctrl-C pressed. Exit\n");
			return;
		}

		if(iteration_count>0&&i>=iteration_count)
		{
			printf("solver iteration count reached\n");
			return;
		}

		steps_t key;
        bool r;
        if(use_ant)
        {
            if(!root_key.empty())r=tr.get_ant_job(root_key,key);
            else r=tr.get_ant_job(key);

        }
        else r=tr.get_job(key);

		if(!r)
		{
			printf("no job anymore\n");
			return;
		}

        if(!use_ant)
        {
		    if(key_len==0)key_len=key.size();
		    else if(key.size()!=key_len)
		    {
#ifdef _WIN32
			    printf("level %u succesefully solved\n",key_len);
#else
			    printf("level %lu succesefully solved\n",key_len);
#endif
			    return;
		    }
        }

		self_solve(tr,key);
	}
}

bool show_state(solution_tree_t& tr,steps_t req)
{
	sol_state_t st;
	data_t bin;
	std::string str;

	st.key=req;

	if(!tr.get(st))
	{
		printf("%s does not exist\n",print_steps(req).c_str());
		return false;
	}
				
	data_t bin_key;
	points2bin(st.key,bin_key);

	std::string h;
	bin2hex(bin_key,h);

	std::string k=print_steps(st.key);
	std::string n=print_points(st.neitrals);
	std::string sw=print_points(st.solved_wins);
	std::string sf=print_points(st.solved_fails);
	std::string tw=print_points(st.tree_wins);
	std::string tf=print_points(st.tree_fails);
	std::string field_str=print_field(req);

	printf("key: %s\nhex_key: %s\nwins_count=%llu fails_count=%llu\nneitrals: %s\nsolved wins: %s\ntree wins: %s\nsolved fails: %s\ntree fails: %s\nfield:\n%s",
        k.c_str(),h.c_str(),st.wins_count,st.fails_count,
		n.c_str(),
		sw.c_str(),tw.c_str(),
		sf.c_str(),tf.c_str(),
		field_str.c_str());

	return true;
}

void save_job(solution_tree_t& tr,const std::string& file_name)
{
	data_t file_content;
	Gomoku::load_file(file_name,file_content);

	std::string content(file_content.begin(),file_content.end());

    std::vector< std::string > lines;
	boost::split( lines, content, boost::is_any_of("\n"));

	for(size_t i=0;i<lines.size();i++)
	{
		std::string& s=lines[i];
		boost::trim(s);

		if(s.empty())
			continue;
		
		std::vector< std::string > parts;
		boost::split( parts, s, boost::is_any_of(";"));

		if(parts.size()!=4)
			throw std::runtime_error("Couldn't parse line: '"+s+"'");

		for(size_t i=0;i<parts.size();i++)
			boost::trim(parts[i]);

		data_t bin;

		steps_t key;
		points_t neitrals;
		npoints_t win;
		npoints_t fails;
		
		hex2bin(parts[0],bin);
		bin2points(bin,key);
		
		hex2bin(parts[1],bin);
		bin2points(bin,neitrals);
		
		hex2bin(parts[2],bin);
		bin2points(bin,win);
		
		hex2bin(parts[3],bin);
		bin2points(bin,fails);
		
		tr.trunc_neitrals(key,neitrals,win,fails);
		tr.save_job(key,neitrals,win,fails);
	}
}

int main(int argc,char** argv)
{
	if(argc<3)
	{
		print_use();
		return 1;
	}

    log_err.open();
        
    log_file.file_name="db.log";
    log_file.print_timestamp=true;
    log_file.open();

    ObjectProgress::log_generator lg(true);

#ifdef _WIN32
	srand(static_cast<unsigned>(time(0)));
#else
	struct timeval time; 
    gettimeofday(&time,NULL);
    srand((time.tv_sec * 1000) + (time.tv_usec / 1000));
#endif

	try
	{

		fs::path root_dir(argv[1]);
		std::string cmd=argv[2];

		isolution_tree_base_ptr db;
		
		const char* mysql_db=getenv("mysql_db");
		
		if(mysql_db!=0&&(*mysql_db)!=0) db=isolution_tree_base_ptr(new Mysql::base_t(mysql_db));
		else  db=isolution_tree_base_ptr(new bin_index_solution_base_t(root_dir.string()));
		 
		solution_tree_t tr(db);
		tr.init(root_dir.string());

		if(!fs::exists(root_dir))
		{
			fs::create_directory(root_dir);
			tr.create_init_tree();
		}


		if(cmd=="get_job" ||cmd=="get_ant_job")
		{
			steps_t key;
            
            
            bool r;
            if(cmd=="get_job") r=tr.get_job(key);
            else
            {
                if(argc<4)r=tr.get_ant_job(key);
                else
                {
                    steps_t root_key;
                    hex_or_str2points(argv[3],root_key);
                    r=tr.get_ant_job(root_key,key);
                }
            }

			if(!r)
			{
				printf("no job anymore\n");
				return 1;
			}

			data_t bin_key;
			points2bin(key,bin_key);

			std::string ret;
			bin2hex(bin_key,ret);
			printf("%s",ret.c_str());
		}
		else if(cmd=="save_job")
		{
			if(argc!=4)
			{
				print_use();
				return 1;
			}

			save_job(tr,argv[3]);
		}
		else if(cmd=="get")
		{
			if(argc!=4)
			{
				print_use();
				return 1;
			}

			sol_state_t st;
			data_t bin;
			std::string str;

			hex2bin(argv[3],bin);
			bin2points(bin,st.key);

			if(!tr.get(st))
			{
				printf("%s does not exist\n",argv[3]);
				return 1;
			}

			points2bin(st.key,bin);
			bin2hex(bin,str);
			if(str.empty())str="empty";
			printf("%s\n",str.c_str());

			points2bin(st.neitrals,bin);
			bin2hex(bin,str);
			if(str.empty())str="empty";
			printf("%s\n",str.c_str());

			points2bin(st.solved_wins,bin);
			bin2hex(bin,str);
			if(str.empty())str="empty";
			printf("%s\n",str.c_str());

			points2bin(st.solved_fails,bin);
			bin2hex(bin,str);
			if(str.empty())str="empty";
			printf("%s\n",str.c_str());

			points2bin(st.tree_wins,bin);
			bin2hex(bin,str);
			if(str.empty())str="empty";
			printf("%s\n",str.c_str());

			points2bin(st.tree_fails,bin);
			bin2hex(bin,str);
			if(str.empty())str="empty";
			printf("%s\n",str.c_str());
		}
		else if(cmd=="view")
		{
			if(argc!=4)
			{
				print_use();
				return 1;
			}

			steps_t req;
            hex_or_str2points(argv[3],req);
			if(!show_state(tr,req))
				return 1;
		}
		else if(cmd=="view_hex")
		{
			if(argc!=4)
			{
				print_use();
				return 1;
			}

			data_t bin;
			hex2bin(argv[3],bin);

			steps_t req;
			bin2points(bin,req);

			if(!show_state(tr,req))
				return 1;
		}
		else if(cmd=="view_root")
		{
			steps_t req=tr.get_root_key();
			if(req.empty())
			{
				printf("no root exists\n");
				return 1;
			}

			if(!show_state(tr,req))
				return 1;
		}
		else if (cmd=="solve_level")
		{
			int iter_count=0;

			if(argc>=4)iter_count=atol(argv[3]);
			self_solve(tr,iter_count);
		}
		else if (cmd=="solve_ant")
		{
			int iter_count=0;
            steps_t root_key;
			
            if(argc>=4)hex_or_str2points(argv[3],root_key);
            if(argc>=5)iter_count=atol(argv[4]);

			self_solve(tr,iter_count,root_key,true);
		}
		else if(cmd=="fix_zero_fails")
		{
            set_ctrl_handler();
            fix_zero_deep_fails_impls pr(tr);

            try
            {
                tr.depth_first_search(pr);
            }
            catch(Gomoku::e_cancel& )
            {
                lg<<"fix_zero_fails canceled";
            }

            lg<<"fix_zero_fails: fixed="<<pr.fixed_count;
		}
		else if(cmd=="relax")
		{
			if(argc!=4)
			{
				print_use();
				return 1;
			}

			steps_t req;
            hex_or_str2points(argv[3],req);

			sol_state_t st;
			st.key=req;
			if(!tr.get(st))
				throw std::runtime_error("state not found");

			if(!st.is_completed())
				throw std::runtime_error("state is not completed");

			tr.relax(st);
		}
		else 
		{
			print_use();
			return 1;
		}
	}
	catch(std::exception& e)
	{
        lg<<"std::exception: "<<e.what();
		return 1;
	}
	catch(...)
	{
        lg<<"unknown exception";
		return 1;
	}

	return 0;
}
