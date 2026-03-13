#include "../rpc.h"
#include "../transport.h"

#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/select.h>

static int orchestrator_running = 1;

enum {
    READ_END = 0,
    WRITE_END = 1
};

// Attempt to read a <pkt> from <tx_fd>, then write the <pkt> to
// the <pkt.header.dst>'th node's pipe.
//
// Returns 0 on success, and -1 on error.
static int forward_pkt(int tx_fd, int pipe_to_node[][2], const unsigned N) {
    pkt_t pkt;
    ssize_t n = read(tx_fd, &pkt, sizeof(pkt));

    if (n != sizeof(pkt))
        return -1;

    if (pkt.header.dst >= N) {
        //fprintf(stderr, "[Orchestrator] Invalid destination: %u (max: %d)\n", 
                pkt.header.dst, N - 1);
        return -1;
    }

    ssize_t m = write(pipe_to_node[pkt.header.dst][WRITE_END], &pkt, n);

    return (m == n) ? 0 : -1;
}

static void shutdown_all_nodes(int pipe_to_node[][2], const int N) {
    pkt_t shutdown_pkt = {
        .header = {
            .code = RPC_SHUTDOWN,
            .dst = N,
            .src = N,
            .payload_n = 0
        }
    };

    // send to all nodes
    for (int i = 0; i < N; i++) {
        ssize_t n = write(pipe_to_node[i][WRITE_END], &shutdown_pkt, sizeof(shutdown_pkt));
        if (n != sizeof(shutdown_pkt)) {
            //fprintf(stderr, "[Orchestrator] Warning: shutdown write to node %d failed\n", i);
        }
    }
}

// Forward messages between nodes, and signal when to terminate processes.
// NICE TO HAVE: don't pass in READ_END of pipe_to_node or WRITE_END of pipe_from_node.
static void orchestra_loop(int pipe_to_node[][2], int pipe_from_node[][2], const int N) {
    // instantiate <fd_set> for fast reset after <select>
    fd_set main_tx_set;
    FD_ZERO(&main_tx_set);

    for (int i = 0; i < N; i++) {
        FD_SET(pipe_from_node[i][READ_END], &main_tx_set);
    }

    uint32_t cnt = 0;
    uint32_t max_msgs = 3;

    while (orchestrator_running) {
        cnt++;
        if (cnt >= max_msgs)
            break;

        // TODO: this is temporary to slow down the process
        sleep(1);

        //fprintf(stderr, "-------------<%d>------------\n", cnt);

        // wait for tx events (only need to watch <pipe_from_node>)
        struct timeval timeout;
        timeout.tv_sec = 6;     // TODO: have an actual end condition

        fd_set tx_set = main_tx_set;
        int ret = select(FD_SETSIZE, &tx_set, NULL, NULL, &timeout);

        if (ret == 0) {
            //fprintf(stderr, "[Orchestrator] Timeout\n");
            break;
        }

        if (ret < 0) {
            //fprintf(stderr, "[Orchestrator] Select error\n");
            break;
        }

        // tx_set now contains the fds that have data to read
        // weird quirck of <select>: need to iterate over all N
        for (int i = 0; i < N; i++) {
            // check if the fd is set in the result
            if (FD_ISSET(pipe_from_node[i][READ_END], &tx_set)) {
                int rc = forward_pkt(pipe_from_node[i][READ_END], pipe_to_node, N);
                if (rc < 0) {
                    //fprintf(stderr, "[Orchestrator] Packet forward unsuccessful\n");
                    orchestrator_running = 0;
                    break;
                }
            }
        }
    }

    //fprintf(stderr, "[Orchestrator] Loop exited, shutting down nodes\n");
    shutdown_all_nodes(pipe_to_node, N);
}

int main(int argc, char **argv) {

    // 0. parse arguments
    if (argc != 2) {
        //fprintf(stderr, "Usage: %s <num_nodes>\n", argv[0]);
        return 1;
    }

    uint32_t N = atoi(argv[1]);

    // 1. set up pipes to/from each node
    int pipe_to_node[N][2];
    int pipe_from_node[N][2];

    for(unsigned i = 0; i < N; i++) {
        if (pipe(pipe_to_node[i]) == -1) {
            perror("pipe to node failed");
            exit(1);
        }

        if (pipe(pipe_from_node[i]) == -1) {
            perror("pipe from node failed");
            exit(1);
        }
    }

    // 2. fork N times, duping pipes to stdin/out
    //    and swapping child execution to node
    pid_t pid;

    for (uint32_t node_id = 0; node_id < N; node_id++) {
        pid = fork();
        
        if (pid == 0) {
            // redirect stdin/stdout to pipe
            dup2(pipe_to_node[node_id][READ_END], STDIN_FILENO);
            dup2(pipe_from_node[node_id][WRITE_END], STDOUT_FILENO);

            // clean up now-obsolete inherited copies of other pipes (ew O(N^2)-ish)
            for (unsigned i = 0; i < N; i++) {
                close(pipe_to_node[i][WRITE_END]);
                close(pipe_to_node[i][READ_END]);
                close(pipe_from_node[i][WRITE_END]);
                close(pipe_from_node[i][READ_END]);
            }

            // swap execution to ./node
            //
            // for passing local int to <execl>:
            // https://stackoverflow.com/questions/49765045/how-to-pass-a-variable-via-exec
            char node_id_str[50];
            snprintf(node_id_str, sizeof(node_id_str), "%d", node_id);
            
            char num_nodes_str[50];
            snprintf(num_nodes_str, sizeof(num_nodes_str), "%d", N);

            execl("./node", "node", node_id_str, num_nodes_str, NULL);

            // shouldn't return
            perror("execl returned");
            exit(1);
        }
    }
    
    // we're the parent now. close unused pipe ends.
    for (unsigned i = 0; i < N; i++) {
        close(pipe_to_node[i][READ_END]);
        close(pipe_from_node[i][WRITE_END]);
    }

    // 3. run the orchestration loop (make sure to signal
    //    children when it's time to terminate)
    orchestra_loop(pipe_to_node, pipe_from_node, N);

    // 4. once the loop is over, wait for the children to finish

    while (wait(NULL) > 0);

    if (errno != ECHILD) {
        perror("unexpected error from wait()\n");
    }

    // 5. clean up rest of pipes
    for (unsigned i = 0; i < N; i++) {
        close(pipe_to_node[i][WRITE_END]);
        close(pipe_from_node[i][READ_END]);
    }

    //fprintf(stderr, "[Orchestrator] Success!\n");

    return 0;
}
