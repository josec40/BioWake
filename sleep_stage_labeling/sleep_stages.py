import pandas as pd

print("Loading Somnus data...")
df = pd.read_csv('labeled_sleep_data_v2.csv')

# At 12.5 Hz, 12.5 rows = 1 second.
total_samples = len(df)
total_minutes = (total_samples / 12.5) / 60
total_hours = total_minutes / 60

counts = df['Label'].value_counts()
percentages = df['Label'].value_counts(normalize=True) * 100

print(f"\nTotal Time Tracked: {int(total_hours)}h {int(total_minutes % 60)}m")
print("\n--- Somnus Prototype Breakdown ---")

for stage in counts.index:
    stage_mins = (counts[stage] / 12.5) / 60
    stage_pct = percentages[stage]
    hours = int(stage_mins // 60)
    mins = int(stage_mins % 60)
    print(f"{stage}: {hours}h {mins}m ({stage_pct:.1f}%)")

print("\n--- Apple Watch Target ---")
print("Awake_Light: ~68.8%")
print("REM: ~18.6%")
print("Deep_Sleep: ~12.6%")