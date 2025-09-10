import subprocess
import os
import sys

print("Run XFOIL.Py")

if len(sys.argv) < 6:
    print("Error: Missing arguments.")
    sys.exit(1)

xfoil_path= sys.argv[1]
airfoil_file_path = sys.argv[2]
output_polar_file = sys.argv[3]
flap_position = sys.argv[4]
flap_angle = sys.argv[5]

commands = f"""
LOAD {airfoil_file_path}
ppar
n
200


GDES
FLAP
{flap_position}
999
0.5
{flap_angle}
exec

oper
v
300000
pacc
{output_polar_file}

iter
150
aseq 
0
20
0.5
aseq 
0
-13
0.5

quit
"""

print(commands)

process = subprocess.Popen(
    [xfoil_path],
    stdin=subprocess.PIPE,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    universal_newlines=True
)


stdout_output, stderr_output = process.communicate(input=commands)

# Тепер результати знаходяться у файлі 'polar_results.txt'
# Ви можете прочитати цей файл за допомогою Python (напр. Pandas або NumPy)
# для подальшого аналізу.
print(f"XFOIL завершив роботу. Результати збережено у 'polar_results.txt'.")

# Приклад читання результатів
if os.path.exists('polar_results.txt'):
    with open('polar_results.txt', 'r') as f:
        print("\nВміст файлу з результатами:")
        print(f.read())

