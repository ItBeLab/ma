from .printColumns import print_columns
import math

class AnalyzeRuntimes:
    def __init__(self):
        self.times = {}
        self.counter = 0

    def register(self, name, pledge, func=lambda x: x.exec_time):
        if not name in self.times:
            self.times[name] = (self.counter, [])
            self.counter += 1
        self.times[name][1].append( (pledge, func) )

    def analyze(self, out_file=None):
        print("runtime analysis:")
        if not out_file is None:
            out_file.write("runtime analysis:\n")
        data = []
        max_before_dot = 0
        for name, (counter, pledges) in self.times.items():
            seconds = round(sum(func(pledge) for pledge, func in pledges), 3)
            max_before_dot = max(max_before_dot, int(math.log10(max(1,seconds))) )
            data.append(["[" + str(counter) + "] " + name, seconds])
        data = [(x, str(" "*int(max_before_dot-int(math.log10(max(1,y))))) + str(y) ) for x,y in data]
        data.sort()
        data.insert(0, ["Module name", "runtime [s]"])
        print_columns(data, out_file)
        if not out_file is None:
            out_file.write("\n")