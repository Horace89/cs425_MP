# This is a helper function which read process parameters from files.
def read_config(para):
    with open('config', 'r') as f:
        min_delay, max_delay = 0, 0
        processes = {}
        addr_dict = {}
        for i, line in enumerate(f):
            args = line.split()
            if i == 0:
                min_delay, max_delay = int(args[0]), int(args[1])
            else:
                id, ip, port = int(args[0]), args[1], int(args[2])
                processes[id] = (ip, port)
                addr_dict[(ip, port)] = id

        if para == 'processes':
            return processes, addr_dict
        elif para == 'delay':
            return min_delay/1000.0, max_delay/1000.0


def get_processes_info():
    return read_config('processes')


def get_delay_info():
    return read_config('delay')