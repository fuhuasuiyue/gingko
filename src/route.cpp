/**
 * route.cpp
 *
 *  Created on: 2011-5-25
 *      Author: auxten
 **/
#ifndef GINGKO_CLNT
#define GINGKO_CLNT
#endif /** GINGKO_CLNT **/

#include <algorithm>

#include "gingko.h"
#include "hash/xor_hash.h"
#include "snap.h"
#include "progress.h"
#include "gingko_clnt.h"
#include "log.h"
#include "async_pool.h"

GINGKO_OVERLOAD_S_HOST_EQ

extern pthread_mutex_t g_blk_hostset_lock;
extern s_gingko_global_t gko;

/**
 * @brief compare by ip distance from the_clnt
 *
 * @see
 * @note
 * @author auxten  <auxtenwpc@gmail.com>
 * @date 2011-8-1
 **/
const bool cmpByDistance(const s_host_t & h1, const s_host_t & h2)
{
    ///return abs(h1.port - 59999) > abs(h2.port - 59999);
    s_host_t * server = & gko_pool::gko_serv;
    return host_distance(server, &h1)
            < host_distance(server, &h2);
}

/**
 * @brief get the possible block source, src_max in max
 *
 * @see
 * @note
 * @author auxten  <auxtenwpc@gmail.com>
 * @date 2011-8-1
 **/
int get_blk_src(s_job_t * jo, unsigned src_max, GKO_INT64 blk_idx,
        std::vector<s_host_t> * h_vec)
{
    s_block_t * b;
    const GKO_INT64 start_idx = blk_idx;

    (*h_vec).clear();
    GKO_UINT64 max = MIN(src_max + 1, (*jo->host_set).size());
    /// count back blk to find the upper stream src
    /// Note that, the src count may be more than the max
    while ((*h_vec).size() < max)
    {
        b = jo->blocks + blk_idx;
        pthread_mutex_lock(&g_blk_hostset_lock);
        if (b->host_set != NULL && (*(b->host_set)).size() != 0)
        {
            for (std::set<s_host_t>::const_iterator i = (*(b->host_set)).begin(); i
                    != (*(b->host_set)).end(); i++)
            {
                if (find((*h_vec).begin(), (*h_vec).end(), *i)
                        == (*h_vec).end())
                {
                    (*h_vec).push_back(*i);
                }
            }
        }
        pthread_mutex_unlock(&g_blk_hostset_lock);
        ///printf("blk_idx: %lld\n", blk_idx);
        blk_idx = prev_b(jo, blk_idx);

        /**
         * in case of dead lock, when a client quit between
         * max is decided and the while loop is started
         */
        if (blk_idx == start_idx)
        {
            break;
        }
    }
    /// sort the src by distance, cause we just need the first src_max+1
    sort((*h_vec).begin(), (*h_vec).end(), cmpByDistance);
    /// insert gko.the_serv
    (*h_vec).push_back(gko.the_serv);
    return (*h_vec).size();
}

/**
 * @brief decide the final src from the host set generated by get_blk_src()
 * @brief return block count downloaded during test the source speed
 *
 * @see
 * @return only return 0 or positive num
 * @note
 * @author auxten  <auxtenwpc@gmail.com>
 * @date 2011-8-1
 **/
int decide_src(s_job_t * jo, int src_max, GKO_INT64 blk_idx,
        std::vector<s_host_t> * h_vec, s_host_t * h, char * buf)
{
    int num;
    int host_i = 0;
    int blk_i = 0;
    struct timeval before_tv;
    struct timeval after_tv;
    GKO_INT64 fastest_time = MAX_INT64;
    GKO_INT64 tmp_time;
    std::vector<s_host_t>::iterator fastest = (*h_vec).end();
    s_block_t * b;

    for (std::vector<s_host_t>::iterator i = (*h_vec).begin();
            i != (*h_vec).end();
            i++)
            {
        /**
         * if the first host in the vector is myself, pass it
         **/
        if (host_distance(&gko_pool::gko_serv, &(*i)) == 0)
        {
            continue;
        }
        /**
         * do not req the server if there is already a available data src
         **/
        if (*i == gko.the_serv)
        {
            if (fastest_time != MAX_INT64)
            {
                continue;
            }
        }
        /**
         * get one block and calculate the time used
         **/
        gettimeofday(&before_tv, NULL);
        num = get_blocks_c(jo, &(*i), (blk_idx + blk_i) % jo->block_count, 1, 0 & ~W_DISK, buf);
        gettimeofday(&after_tv, NULL);
        b = jo->blocks + (blk_idx + blk_i) % jo->block_count;
        if (num == 1 && digest_ok(buf, b))
        {
            if (UNLIKELY(writeblock(jo, (u_char *) buf, b) < 0))
            {
                return 0;
            }
            else
            {
                b->done = 1;
                dump_progress(jo, b);
            }
            blk_i++;
            tmp_time = (after_tv.tv_sec - before_tv.tv_sec) * 1000000LL
                    + after_tv.tv_usec - before_tv.tv_usec;
        }
        else
        {
            /**
             * if get no block, make its time longest
             **/
            tmp_time = MAX_INT64;
        }
//        gko_log(DEBUG, "%u time: %lld", (*i).port, tmp_time);
        if (fastest_time > tmp_time)
        {
            fastest_time = tmp_time;
            fastest = i;
        }
        if (++host_i == src_max)
        {
            ///make i point to the "gko.the_serv" prev, the ++ will make it serv
            i = (*h_vec).end() - 2;
//            gko_log(DEBUG, "next: %u", (*i).port);
            continue;
        }
    }
    ///get NO available src
    if (fastest == (*h_vec).end())
    {
        return 0;
    }
    memset(h, 0, sizeof(s_host_t));
    memcpy(h, &(*fastest), sizeof(s_host_t));
    ///printf("choose %s:%d\n", h->addr, h->port);
    return blk_i;
}
