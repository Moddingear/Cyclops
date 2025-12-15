import math
fps = 24

timestamps = [(50, "Robotronik, Match 2"), (672, "RIR, Match 3"), (1269, "ESEO, Match 4"), (1885, "GNOLE, Match 5"), (2483, "Intech, 8e de finale"), (3092, "IFEA, quart de finale"), (3703, "7Robot, demie finale"), (4331, "ESEO finale 1"), (4950, "ESEO, finale 2"), (5598, "Barrage Eurobot"), (6275, "UCLouvain, huitieme de finale Eurobot")]

for time, name in timestamps:
    seconds = time//fps
    minutes = seconds//60
    seconds = seconds %60
    print(f"{minutes:02d}:{seconds:02d} {name}")