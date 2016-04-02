import multiprocessing
import socket
import argparse
import configreader
from channel import Channel
from CausalOrderChannel import CausalOrderChannel
from TotalOrderChannel import TotalOrderChannel


class Process(multiprocessing.Process):
    """
        This process could take arguments from command line and execute unicast/multicast actions.
        It can also receive messages from its channel.
    """

    def __init__(self, id, ordering):
        super(Process, self).__init__() # call __init__ from multiprocessing.Process
        self.id = id

        # Read config from file
        self.process_info, self.addr_dict = configreader.get_processes_info()
        address = self.process_info[id]
        ip, port = address[0], address[1]
        print(ip, port)

        # Init a socket
        self.socket = socket.socket(
            socket.AF_INET, socket.SOCK_DGRAM)
        self.socket.bind(address)

        # Init channel

        # Causal Order Channel
        if ordering == 'causal':
            print 'causal ordering'
            self.channel = CausalOrderChannel(self, self.id, self.socket, self.process_info, self.addr_dict)
        elif ordering == 'total':
            print 'total ordering'
            # Total Order Channel
            if id == 1:
                # Select process 1 to be the sequencer
                self.channel = TotalOrderChannel(self, self.id, self.socket, self.process_info, self.addr_dict, True)
            else:
                self.channel = TotalOrderChannel(self, self.id, self.socket, self.process_info, self.addr_dict)
        else:
            # Regular channel
            print 'no ordering'
            self.channel = Channel(self, self.id, self.socket, self.process_info, self.addr_dict)

    def run(self):
        while True:
            data, address = self.socket.recvfrom(4096)
            self.channel.recv(data, address)

    def unicast_receive(self, source, message):
        print (message.receive_str())

    def unicast(self, destination, message):
        self.channel.unicast(message, destination)

    def multicast(self, message):
        self.channel.multicast(message)


def main():
    parser = argparse.ArgumentParser(description='Process some integers.')
    parser.add_argument("id", help="process id", type=int, default=1)
    parser.add_argument("ordering", help="process id", type=str, default='causal')
    args = parser.parse_args()

    p = Process(args.id, args.ordering)
    p.daemon = True # daemon thread does not prevent main program from exiting
    p.start()

    while True:
        cmd = raw_input()
        if cmd:
            cmd_args = cmd.split()
            if cmd_args[0] == "exit": break;
            elif cmd_args[0] == "send":
                destination, message = int(cmd_args[1]), cmd_args[2]
                p.unicast(destination, message)
            elif cmd_args[0] == "msend":
                message = cmd_args[1]
                p.multicast(message)

if __name__ == '__main__':
    main()
