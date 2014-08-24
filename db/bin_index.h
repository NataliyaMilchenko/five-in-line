#ifndef bin_indexH
#define bin_indexH
#include "paged_file_access.h"
#include "ibin_index.h"
#include <map>
#include "../extern/exception_catch.h"
#include <boost/iterator/counting_iterator.hpp>

namespace Gomoku
{

class bin_index_t : public ibin_index_t
{
public:
	inline static size_t idx_rec_len(size_t key_len){return key_len+2*sizeof(file_offset_t)+sizeof(size_t);}
	inline static size_t idx_page_len(size_t key_len,size_t page_max){return (page_max+1)*idx_rec_len(key_len);}

	typedef boost::counting_iterator<file_offset_t> page_iter;
	struct index_t;

	struct index_ref
	{
		unsigned char* const data;
		const size_t key_len;

		index_ref(unsigned char* _data,size_t _key_len) : data(_data),key_len(_key_len)
		{
		}

		file_offset_t& left(){return *reinterpret_cast<file_offset_t*>(data);}
		file_offset_t& data_offset(){return *reinterpret_cast<file_offset_t*>(data+sizeof(file_offset_t));}
		size_t& data_len(){return *reinterpret_cast<size_t*>(data+2*sizeof(file_offset_t));}
		unsigned char* key_begin(){return data+2*sizeof(file_offset_t)+sizeof(size_t);}
		unsigned char* key_end(){return key_begin()+key_len;}

		const file_offset_t& left() const {return const_cast<index_ref*>(this)->left();}
		const file_offset_t& data_offset() const {return const_cast<index_ref*>(this)->data_offset();}
		const size_t& data_len() const {return const_cast<index_ref*>(this)->data_len();}
		const unsigned char* key_begin() const {return const_cast<index_ref*>(this)->key_begin();}
		const unsigned char* key_end() const{return const_cast<index_ref*>(this)->key_end();}

		void copy_pointers(const index_t& rhs)
		{
			left()=rhs.left;
			data_offset()=rhs.data_offset;
			data_len()=rhs.data_len;
		}

		void copy_key(const index_t& rhs)
		{
			std::copy(rhs.key.begin(),rhs.key.end(),key_begin());
		}

		void operator=(const index_t& rhs)
		{
			copy_pointers(rhs);
			copy_key(rhs);
		}
	};

	struct index_t
	{
		data_t key;
		file_offset_t left;
		file_offset_t data_offset;
		size_t data_len;

		file_offset_t page_offset;
		size_t index_in_page;


		index_t()
		{
			left=0;
			data_offset=0;
			data_len=0;
			
			page_offset=0;
			index_in_page=0;
		}

		void copy_pointers(const index_ref& rhs)
		{
			left=rhs.left();
			data_offset=rhs.data_offset();
			data_len=rhs.data_len();
		}

		void copy_key(const index_ref& rhs)
		{
			key.clear();
			key.insert(key.end(),rhs.key_begin(),rhs.key_end());
		}

		void operator=(const index_ref& rhs)
		{
			copy_pointers(rhs);
			copy_key(rhs);
		}
	};

	class page_t
	{
	public:
		data_t buffer;
		const size_t key_len;
		const size_t page_max;
		
		file_offset_t page_offset;
		bool dirty;

		page_t(size_t _key_len,size_t _page_max) : key_len(_key_len),page_max(_page_max)
		{
			buffer.resize(idx_page_len(key_len,page_max),0);
			page_offset=0;
			dirty=false;
		}

		page_iter begin() const{return page_iter(0);}
		page_iter end() const{return page_iter(items_count());}

		inline index_ref operator[](size_t idx){return index_ref(&buffer[idx_rec_len(key_len)*idx],key_len);}
		inline const size_t& items_count() const{return const_cast<page_t&>(*this)[page_max].data_len();}
		inline size_t& items_count(){return const_cast<page_t&>(*this)[page_max].data_len();}

		void insert_item(const index_t& val)
		{
			size_t ct=items_count();

			data_t::iterator from=buffer.begin()+idx_rec_len(key_len)*val.index_in_page;
			data_t::iterator to=buffer.begin()+idx_rec_len(key_len)*(ct+1);
			std::copy_backward(from,to,to+idx_rec_len(key_len));

			index_ref r=operator[](val.index_in_page);
			r=val;

			items_count()=ct+1;
			dirty=true;
		}

		void dump();
	};

	typedef boost::shared_ptr<page_t> page_ptr;

	class page_pr
	{
		page_t& page;

	public:
		page_pr(page_t& _page) : page(_page)
		{
		}

		inline bool operator()(file_offset_t ia,file_offset_t ib) const
		{
			index_ref a=page[static_cast<size_t>(ia)];
			index_ref b=page[static_cast<size_t>(ib)];
			return std::lexicographical_compare(a.key_begin(),a.key_end(),b.key_begin(),b.key_end());
		}

		inline bool operator()(file_offset_t ia,const data_t& b) const
		{
			index_ref a=page[static_cast<size_t>(ia)];
			return std::lexicographical_compare(a.key_begin(),a.key_end(),b.begin(),b.end());
		}

		inline bool operator()(const data_t& a,file_offset_t ib) const
		{
			index_ref b=page[static_cast<size_t>(ib)];
			return std::lexicographical_compare(a.begin(),a.end(),b.key_begin(),b.key_end());
		}
	};

	struct data_less_pr
	{
		inline bool cmp(const data_t& a,const data_t& b) const
		{
			return std::lexicographical_compare(a.begin(),a.end(),b.begin(),b.end());
		}

		inline bool operator()(const data_t& a,const data_t& b) const{return cmp(a,b);}
		inline bool operator()(const index_t& a,const index_t& b) const{return cmp(a.key,b.key);}
		inline bool operator()(const index_t& a,const data_t& b) const{return cmp(a.key,b);}
		inline bool operator()(const data_t& a,const index_t& b) const{return cmp(a,b.key);}
	};

	class inode
	{
		inode(const inode&);
		void operator=(const inode&);
	protected:
		const bin_index_t& parent;
		const std::string base_dir;
		const size_t key_len;

        void validate_key_len(const data_t& key) const;
	public:
		inode(const bin_index_t& _parent,const std::string& _base_dir,size_t _key_len) 
			: parent(_parent),base_dir(_base_dir),key_len(_key_len)
		{
		}

		virtual ~inode(){}

		virtual bool get(const data_t& key,data_t& val) const=0;
		virtual bool set(const data_t& key,const data_t& val)=0;
		virtual bool first(data_t& key,data_t& val) const=0;
		virtual bool next(data_t& key,data_t& val) const=0;

		virtual bool is_valid() const{return true;}
	};

	typedef boost::shared_ptr<inode> node_ptr;

	class file_node : public inode
	{
		bool self_valid;
		mutable file_access_ptr fi;
		mutable file_access_ptr fd;
		mutable page_ptr root_page;
		mutable file_offset_t items_count;

		void load_data(file_offset_t offset,data_t& res) const;
		void save_data(file_offset_t offset,const data_t& res);
		file_offset_t append_data(const data_t& res);

		void load_index_data(file_offset_t offset,data_t& res) const;
		void save_index_data(file_offset_t offset,const data_t& res);
		void save_index_data(file_offset_t offset,file_offset_t res);
		file_offset_t append_index_data(const data_t& res);

		void split();
		void close_files();
		void open_index_file() const;
		void open_data_file() const;
		std::string index_file_name() const;
		std::string data_file_name() const;
		void validate_root_offset() const;

		void load_page(page_t& val) const{load_index_data(val.page_offset,val.buffer);}
		void save_page(page_t& val){save_index_data(val.page_offset,val.buffer);val.dirty=false;}
		void flush_page(page_t& val){if(val.dirty)save_page(val);}
		void append_page(page_t& val){val.page_offset=append_index_data(val.buffer);val.dirty=false;}

		bool get_item(page_t& page,index_t& val) const;
		bool first_item(page_t& page,index_t& val) const;
		bool next_item(page_t& page,index_t& val) const;
		bool next_item(page_t& page,const page_iter& p,index_t& val) const;
		void add_item(const index_t& val);
		void add_item(const index_t& val,page_t& page);
		void split_page(page_t& child_page,page_t& parent_page,size_t parent_index,page_t& new_right_page);
	public:
		bool disable_split;

		file_node(const bin_index_t& _parent,const std::string& _base_dir,size_t _key_len) 
			: inode(_parent,_base_dir,_key_len)
		{
			self_valid=true;
			disable_split=false;
			items_count=0;
		}

		~file_node(){close_files();}

		bool get(const data_t& key,data_t& val) const;
		bool set(const data_t& key,const data_t& val);
		bool first(data_t& key,data_t& val) const;
		bool next(data_t& key,data_t& val) const;
		virtual bool is_valid() const{return self_valid;}
		void update_page(const index_t& it);
	};

	class dir_node : public inode
	{
		mutable datas_t indexes;
		mutable bool index_valid;
		
		mutable node_ptr sub_node;
		mutable data_t sub_name;


		void validate_index() const;
		void load_index() const;
		void validate_sub(const data_t& name) const;
		void create_text_child(const data_t& name) const;
	public:
		dir_node(const bin_index_t& _parent,const std::string& _base_dir,size_t _key_len) 
			: inode(_parent,_base_dir,_key_len)
		{
			index_valid=false;
		}

		bool get(const data_t& key,data_t& val) const;
		bool set(const data_t& key,const data_t& val);
		bool first(data_t& key,data_t& val) const;
		bool next(data_t& key,data_t& val) const;
	};
private:
	const size_t key_len;
	const size_t dir_key_len;
	const file_offset_t file_max_records;
	const size_t page_max;
	const std::string base_dir;
	mutable node_ptr root;
	file_offset_t items_count;
	paged_file_provider_t file_provider;

	void validate_root() const;
	node_ptr create_node(const std::string& base_dir,size_t key_len) const;

	void load_items_count();
	void save_items_count();
public:
	std::string index_file_name;
	std::string data_file_name;
	std::string items_count_file_name;

	bin_index_t(const std::string& _base_dir,size_t _key_len,size_t _dir_key_len=1,file_offset_t _file_max_records=1048576,size_t _page_max=512);
	~bin_index_t()
    {
        try
        {
            save_items_count();
        }
        UNCATCHED_EXCEPTION_CATCH;
    }

	
	bool get(const data_t& key,data_t& val) const
	{
		validate_root();
		return root->get(key,val);
	}

	bool set(const data_t& key,const data_t& val)
	{
		validate_root();
		bool r=root->set(key,val);
		if(!root->is_valid())root.reset();
		
		if(r)
		{
			++items_count;
			if((items_count%1024)==0)save_items_count();
		}
		
		return r;
	}

	bool first(data_t& key,data_t& val) const
	{
		validate_root();
		return root->first(key,val);
	}

	bool next(data_t& key,data_t& val) const
	{
		validate_root();
		return root->next(key,val);
	}

	inline const ifile_access_provider_t& get_file_provider() const{return file_provider;}
};

class bin_indexes_t : public ibin_indexes_t
{
public:
  typedef boost::shared_ptr<bin_index_t> item_ptr;
  typedef std::map<unsigned,item_ptr> indexes_t;
private:
  const std::string base_dir;
  const size_t len_per_level;
  indexes_t indexes;
public:
  bin_indexes_t(const std::string& _base_dir,size_t _len_per_level) : base_dir(_base_dir),len_per_level(_len_per_level) {}

  unsigned get_root_level() const;
  bin_index_t& get_index(unsigned steps_count);
  bin_index_t* find_index(unsigned steps_count);
};



}//namespace

#endif
