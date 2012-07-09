#include <iostream>

#include <vector>
#include <utility>

using namespace std;

class reg_key {
    public:
        long *regkey_buf;
        int regkey_len;
        int regkey_lkey;
        int regkey_rkey;
};

class reg_cache {

    public:
        reg_cache();
        ~reg_cache();
        reg_key reg_cache_find(int, long *, int);
        int reg_cache_insert(int, long *, int, int, int);
        vector< vector<reg_key> > dreg_cache;
};

