import pandas as pd
import numpy as np

print("Loading data...")
df = pd.read_csv('sleep_data.csv')

# 1. Bigger Window for Variance (e.g., 5 minutes = 3750 rows at 12.5Hz)
window_size = 3750

print("Smoothing raw signals and calculating variance...")
df['Motion'] = abs(df['Accel_X']) + abs(df['Accel_Y']) + abs(df['Accel_Z'])

# Smooth the raw motion first to kill micro-jitters, then get variance
df['Motion_Smooth'] = df['Motion'].rolling(window=200, center=True).mean()
df['Motion_Var'] = df['Motion_Smooth'].rolling(window=window_size, center=True).std()

# Smooth IR first to kill light-leak jitters, then get variance
df['IR_Smooth'] = df['IR'].rolling(window=200, center=True).mean()
df['IR_Var'] = df['IR_Smooth'].rolling(window=window_size, center=True).std()

df.fillna(method='bfill', inplace=True)
df.fillna(method='ffill', inplace=True)

# 2. Adjust Quantiles (Force the script to be pickier)
# Set to 0.32 (Meaning the top 68% of motion activity is Awake/Light)
motion_threshold = df['Motion_Var'].quantile(0.32)
# Set to 0.15 (Meaning a massive 85% of heart-rate variance is eligible for REM)
ir_threshold = df['IR_Var'].quantile(0.15)
def assign_sleep_stage(row):
    if row['Motion_Var'] > motion_threshold:
        return 3 # Awake_Light
    elif row['Motion_Var'] <= motion_threshold and row['IR_Var'] > ir_threshold:
        return 2 # REM
    else:
        return 1 # Deep_Sleep

print("Assigning raw stages...")
df['Raw_Label_Num'] = df.apply(assign_sleep_stage, axis=1)

print("Applying heavy block-smoothing (This might take 30-60 seconds)...")
# Use a massive 10-minute rolling window to find the most common stage (mode)
# 10 mins * 60 sec * 12.5 Hz = 7500 rows. This forces the hypnogram into solid terraces.
# Change the window from 3750 to 1875 (2.5 minutes at 12.5Hz)
df['Smoothed_Label_Num'] = df['Raw_Label_Num'].rolling(window=1875, center=True).apply(lambda x: x.mode()[0] if not x.mode().empty else np.nan)
df.fillna(method='bfill', inplace=True)
df.fillna(method='ffill', inplace=True)

# Map the numbers back to the strings Edge Impulse needs
stage_map = {1: 'Deep_Sleep', 2: 'REM', 3: 'Awake_Light'}
df['Label'] = df['Smoothed_Label_Num'].map(stage_map)

# Cleanup and save
print("Cleaning up file...")
df_clean = df.drop(columns=['Motion', 'Motion_Smooth', 'Motion_Var', 'IR_Smooth', 'IR_Var', 'Raw_Label_Num', 'Smoothed_Label_Num'])
df_clean.to_csv('labeled_sleep_data_v2.csv', index=False)

print("Done! Saved as labeled_sleep_data_v2.csv")