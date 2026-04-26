import pandas as pd
import matplotlib.pyplot as plt

print("Loading labeled data...")
df = pd.read_csv('labeled_sleep_data_v2.csv')

# Map string labels to numbers so we can plot them on a graph
stage_map = {'Deep_Sleep': 1, 'REM': 2, 'Awake_Light': 3}
df['Stage_Num'] = df['Label'].map(stage_map)

# Calculate percentages to see if they make biological sense
percentages = df['Label'].value_counts(normalize=True) * 100

print("\n--- Sleep Stage Breakdown ---")
print(percentages.round(2))
print("-----------------------------\n")
print("Normal Human Averages:")
print("Awake/Light: ~50-60%")
print("Deep Sleep:  ~15-25%")
print("REM:         ~20-25%\n")

# Plot the Hypnogram
plt.figure(figsize=(15, 5))
# Since plotting 270,000 points is heavy, we take a rolling mode or just plot every 100th point
df_plot = df.iloc[::100, :] 
plt.plot(df_plot.index, df_plot['Stage_Num'], drawstyle='steps-mid', color='b', linewidth=2)
plt.yticks([1, 2, 3], ['Deep Sleep', 'REM', 'Awake/Light'])
plt.title("Jose's Sleep Architecture (Heuristic Hypnogram)")
plt.xlabel("Time (Samples)")
plt.ylabel("Sleep Stage")
plt.grid(axis='y', linestyle='--', alpha=0.7)
plt.show()