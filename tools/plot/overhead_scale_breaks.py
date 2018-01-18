#!/usr/bin/env python3

# Uncomment this for generating PDF without a X server
import matplotlib as mpl
mpl.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
from pprint import pprint
import sys
import numpy
import os
import math

if len(sys.argv) <= 2:
	sys.exit('\nUsage:\n\tpython3 "' + sys.argv[0] + '" <base_time_log> <time_log1> [<time_log2> ...]\n')

def ParseBaseTimeLog(FileName):
	BenchName = []
	List = []
	with open(FileName) as Fin:
		for Line in Fin:
			if Line[0] == '#':
				continue
			(Time, Bench) = Line.split('\t')
			BenchName.append(Bench.strip())
			List.append(float(Time))
	if len(BenchName) == 0:
		sys.exit("no bench found")
	BenchName.append("G-Mean")
	return (BenchName, List)

def ParseTimeLog(FileName):
	List = []
	with open(FileName) as Fin:
		for Line in Fin:
			if Line[0] == '#':
				continue
			(Time, Bench) = Line.split('\t')
			List.append(float(Time))
	return List

def NormalizeTimeList(Log, BaseLog):
	if len(BaseLog) != len(Log):
		sys.exit("List length mismatch")
	Count = 0
	GMean = 1.0
	for BenchIdx in range(0, len(BaseLog)):
		Log[BenchIdx] = Log[BenchIdx] / BaseLog[BenchIdx]
		if Log[BenchIdx] > 0:
			Count += 1
			GMean *= Log[BenchIdx]
			sys.stdout.write(" %6.2f" % Log[BenchIdx])
	GMean = math.pow(GMean, 1.0 / Count)
	sys.stdout.write("; GMean: %.2f\n" % GMean)
	Log.append(GMean)

BaseTimeLog = ParseBaseTimeLog(sys.argv[1])
TimeLog = []
Index = numpy.arange(len(BaseTimeLog[0]))

plt.rcParams.update({'font.size': 12.5})
f, (Axe, Axe2, Axe3) = plt.subplots(3, 1, figsize=(27, 5.5), sharex=True)

Axe3.set_xlabel("Benchmarks")
Axe2.set_ylabel("Normalized Execution Time")
#plt.yscale('log')

Width = 0.12
AccWidth = 0

Bar = ()
BarName = ()

for ArgIdx in range(2, len(sys.argv)):
	ConfigName = os.path.basename(os.path.dirname(sys.argv[ArgIdx]))
	sys.stdout.write("%11s |" % ConfigName)
	NewTimeLog = ParseTimeLog(sys.argv[ArgIdx])
	NormalizeTimeList(NewTimeLog, BaseTimeLog[1])
	Rect = Axe.bar(Index + AccWidth, NewTimeLog, Width)
	Axe2.bar(Index + AccWidth, NewTimeLog, Width)
	Axe3.bar(Index + AccWidth, NewTimeLog, Width)
	Bar = Bar + (Rect[0], )
	BarName = BarName + (ConfigName, )
	AccWidth += Width

plt.tight_layout()
plt.figlegend(Bar, BarName, loc=1, bbox_to_anchor=(0.985, 0.95))

Axe.set_xticks(Index + ((len(sys.argv) - 3) * Width) / 2)
Axe.set_xticklabels(BaseTimeLog[0])

#pprint(TimeLog)
#sys.exit(":)")

Axe.plot()
Axe2.plot()
Axe3.plot()
plt.subplots_adjust(hspace=0.05)

Axe.set_ylim(191, 205)
Axe2.set_ylim(23, 37)
Axe3.set_ylim(0, 14)

Axe.spines['bottom'].set_visible(False)
Axe2.spines['top'].set_visible(False)
Axe2.spines['bottom'].set_visible(False)
Axe3.spines['top'].set_visible(False)

Axe.tick_params(axis='x', labelbottom='off', bottom='off')
Axe2.tick_params(axis='x', labelbottom='off', bottom='off')


Axe3.xaxis.tick_bottom()

Axe.set_axisbelow(True)
Axe.grid('on', ls=':')
Axe2.set_axisbelow(True)
Axe2.grid('on', ls=':')
Axe3.set_axisbelow(True)
Axe3.grid('on', ls=':')

d = 0.005  # how big to make the diagonal lines in axes coordinates
# arguments to pass to plot, just so we don't keep repeating them
kwargs = dict(transform=Axe.transAxes, color='k', clip_on=False)
Axe.plot((-d, +d), (-d, +d), **kwargs)
Axe.plot((1 - d, 1 + d), (-d, +d), **kwargs)

kwargs.update(transform=Axe2.transAxes)
Axe2.plot((-d, +d), (-d, +d), **kwargs)
Axe2.plot((1 - d, 1 + d), (-d, +d), **kwargs)
Axe2.plot((-d, +d), (1 - d, 1 + d), **kwargs)
Axe2.plot((1 - d, 1 + d), (1 - d, 1 + d), **kwargs)

kwargs.update(transform=Axe3.transAxes)
Axe3.plot((-d, +d), (1 - d, 1 + d), **kwargs)
Axe3.plot((1 - d, 1 + d), (1 - d, 1 + d), **kwargs)

#plt.show()
plt.savefig("plot.pdf")
