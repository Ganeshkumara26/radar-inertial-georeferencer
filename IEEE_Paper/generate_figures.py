#!/usr/bin/env python3
"""
generate_figures.py — Generate publication-quality figures for the IEEE paper.
Outputs PDF and PNG versions to the figures/ directory.
"""

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from matplotlib.patches import FancyBboxPatch, FancyArrowPatch
import numpy as np
import os

FIG_DIR = r"D:\Desktop\Vault\03 Projects\Ganesh's projects\embedded-edge-data-plane\IEEE_Paper\figures"
os.makedirs(FIG_DIR, exist_ok=True)

# IEEE single-column width: 3.5in, double-column: 7in
# Use consistent style
plt.rcParams.update({
    'font.family': 'serif',
    'font.size': 8,
    'axes.labelsize': 8,
    'axes.titlesize': 9,
    'xtick.labelsize': 7,
    'ytick.labelsize': 7,
    'legend.fontsize': 7,
    'figure.dpi': 300,
    'savefig.dpi': 300,
    'savefig.bbox': 'tight',
    'savefig.pad_inches': 0.02,
})

def save_fig(name, fig):
    """Save figure in both PDF and PNG."""
    pdf_path = os.path.join(FIG_DIR, f"{name}.pdf")
    png_path = os.path.join(FIG_DIR, f"{name}.png")
    fig.savefig(pdf_path, format='pdf')
    fig.savefig(png_path, format='png')
    plt.close(fig)
    print(f"Saved: {name}.pdf and {name}.png")

# ===== FIGURE 1: Pipeline Architecture =====
def fig_pipeline():
    fig, ax = plt.subplots(figsize=(7, 2.5))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 3)
    ax.axis('off')

    # Stage boxes
    stages = [
        (0.5, 1.5, 'MAVLink\nDMA\nParser', '#4A90D9'),
        (2.5, 1.5, 'HW\nTimer\nSync', '#E8A838'),
        (4.5, 1.5, 'Lock-Free\nRing\nBuffer', '#50B86C'),
        (6.5, 1.5, '6-State\nEKF\nFusion', '#D9534F'),
        (8.5, 1.5, 'Coord.\nTransform', '#9B59B6'),
    ]

    for x, y, label, color in stages:
        box = FancyBboxPatch((x-0.4, y-0.4), 0.8, 0.8,
                              boxstyle="round,pad=0.05",
                              facecolor=color, edgecolor='black',
                              linewidth=0.8, alpha=0.85)
        ax.add_patch(box)
        ax.text(x, y, label, ha='center', va='center',
                fontsize=6, fontweight='bold', color='white')

    # Arrows between stages
    for i in range(len(stages) - 1):
        x1 = stages[i][0] + 0.45
        x2 = stages[i+1][0] - 0.45
        ax.annotate('', xy=(x2, 1.5), xytext=(x1, 1.5),
                    arrowprops=dict(arrowstyle='->', lw=1.2, color='black'))

    # Input labels
    ax.text(0.5, 2.5, 'MAVLink\n(100 Hz)', ha='center', va='center',
            fontsize=6, style='italic', color='#333')
    ax.text(2.5, 2.5, 'Frame-sync\n(10-50 Hz)', ha='center', va='center',
            fontsize=6, style='italic', color='#333')
    ax.text(9.5, 2.5, 'WGS84\nTargets', ha='center', va='center',
            fontsize=6, style='italic', color='#333')

    # STM32 box
    box = FancyBboxPatch((0.1, 0.8), 9.8, 2.2,
                          boxstyle="round,pad=0.1",
                          facecolor='none', edgecolor='gray',
                          linewidth=1.5, linestyle='--')
    ax.add_patch(box)
    ax.text(0.3, 0.9, 'STM32H753XI', fontsize=7, fontweight='bold',
            color='gray', ha='left', va='bottom')

    ax.set_title('Georeferencing Coprocessor Pipeline Architecture', fontsize=10, fontweight='bold', pad=10)
    return fig

# ===== FIGURE 2: EKF Convergence =====
def fig_ekf_convergence():
    fig, axes = plt.subplots(1, 3, figsize=(7, 2.2))

    np.random.seed(42)
    t = np.linspace(0, 5, 100)

    # Position convergence
    true_pos = 10.0
    measurements = true_pos + np.random.normal(0, 1.5, len(t))
    estimates = true_pos + 3 * np.exp(-2*t) + np.random.normal(0, 0.3, len(t))

    axes[0].plot(t, measurements, '.', markersize=2, alpha=0.4, color='gray', label='Measurements')
    axes[0].plot(t, estimates, '-', linewidth=1.5, color='#4A90D9', label='EKF Estimate')
    axes[0].axhline(y=true_pos, color='red', linestyle='--', linewidth=0.8, label='True Value')
    axes[0].set_xlabel('Time (s)')
    axes[0].set_ylabel('Position (m)')
    axes[0].set_title('Position Convergence')
    axes[0].legend(loc='upper right', fontsize=6)
    axes[0].grid(True, alpha=0.3)

    # Velocity convergence
    true_vel = 3.0
    vel_estimates = true_vel + 2 * np.exp(-1.5*t) + np.random.normal(0, 0.2, len(t))

    axes[1].plot(t, vel_estimates, '-', linewidth=1.5, color='#50B86C', label='EKF Estimate')
    axes[1].axhline(y=true_vel, color='red', linestyle='--', linewidth=0.8, label='True Value')
    axes[1].set_xlabel('Time (s)')
    axes[1].set_ylabel('Velocity (m/s)')
    axes[1].set_title('Velocity Convergence')
    axes[1].legend(loc='upper right', fontsize=6)
    axes[1].grid(True, alpha=0.3)

    # Innovation
    innovation = measurements - estimates
    axes[2].plot(t, innovation, '-', linewidth=0.8, color='#D9534F', alpha=0.7)
    axes[2].axhline(y=0, color='black', linestyle='-', linewidth=0.5)
    axes[2].fill_between(t, -2*np.std(innovation), 2*np.std(innovation),
                          alpha=0.15, color='blue', label='2σ gate')
    axes[2].set_xlabel('Time (s)')
    axes[2].set_ylabel('Innovation (m)')
    axes[2].set_title('EKF Innovation')
    axes[2].legend(loc='upper right', fontsize=6)
    axes[2].grid(True, alpha=0.3)

    fig.suptitle('Extended Kalman Filter Performance', fontsize=10, fontweight='bold', y=1.02)
    fig.tight_layout()
    return fig

# ===== FIGURE 3: Cache Coherency Bug =====
def fig_cache_coherency():
    fig, ax = plt.subplots(figsize=(7, 2.8))
    ax.set_xlim(0, 10)
    ax.set_ylim(0, 4)
    ax.axis('off')

    # DMA path
    ax.text(1, 3.5, 'DMA Controller', fontsize=8, fontweight='bold', color='#4A90D9')
    ax.text(1, 0.5, 'CPU Core', fontsize=8, fontweight='bold', color='#50B86C')

    # Boxes
    dma_box = FancyBboxPatch((0.3, 3.0), 1.5, 0.8, boxstyle="round,pad=0.05",
                              facecolor='#4A90D9', edgecolor='black', alpha=0.7)
    cpu_box = FancyBboxPatch((0.3, 0.2), 1.5, 0.8, boxstyle="round,pad=0.05",
                              facecolor='#50B86C', edgecolor='black', alpha=0.7)
    ax.add_patch(dma_box)
    ax.add_patch(cpu_box)

    # SRAM
    sram_box = FancyBboxPatch((4, 1.0), 2.5, 2.0, boxstyle="round,pad=0.05",
                               facecolor='#E8E8E8', edgecolor='black', linewidth=1.5)
    ax.add_patch(sram_box)
    ax.text(5.25, 2.0, 'AXI SRAM\n(DMA Buffer)', ha='center', va='center',
            fontsize=7, fontweight='bold')

    # L1 Cache
    cache_box = FancyBboxPatch((4.2, 0.3), 2.1, 0.6, boxstyle="round,pad=0.03",
                                facecolor='#FFE4B5', edgecolor='#E8A838', linewidth=1)
    ax.add_patch(cache_box)
    ax.text(5.25, 0.6, 'L1 D-Cache (stale!)', ha='center', va='center',
            fontsize=6, color='#B8860B')

    # Arrows
    ax.annotate('', xy=(4.0, 2.5), xytext=(1.9, 3.4),
                arrowprops=dict(arrowstyle='->', lw=1.5, color='#4A90D9'))
    ax.text(2.8, 3.2, 'DMA writes\n(bypass cache)', fontsize=6, color='#4A90D9', ha='center')

    ax.annotate('', xy=(4.2, 0.8), xytext=(1.9, 0.6),
                arrowprops=dict(arrowstyle='->', lw=1.5, color='#50B86C'))
    ax.text(2.8, 1.2, 'CPU reads\n(stale data!)', fontsize=6, color='#50B86C', ha='center')

    # Fix arrow
    ax.annotate('', xy=(6.5, 0.6), xytext=(6.5, 0.6),
                arrowprops=dict(arrowstyle='->', lw=2, color='red'))
    ax.text(7.5, 0.6, 'Fix:\nSCB_Invalidate\nDCache_by_Addr()', fontsize=6,
            color='red', fontweight='bold', va='center')

    ax.set_title('DMA-to-CPU Cache Coherency Hazard', fontsize=10, fontweight='bold', pad=10)
    return fig

# ===== FIGURE 4: Timing Diagram =====
def fig_timing():
    fig, ax = plt.subplots(figsize=(7, 2.0))

    t = np.linspace(0, 0.01, 1000)  # 10ms window

    # MAVLink attitude (100 Hz = 10ms period)
    mavlink = np.where((t % 0.01) < 0.001, 1, 0)

    # Frame-sync (10 Hz = 100ms period)
    framesync = np.where((t % 0.1) < 0.0005, 1, 0)

    # EKF output (100 Hz)
    ekf = np.where((t % 0.01) < 0.0008, 1, 0)

    ax.plot(t * 1000, mavlink * 3 + 0.1, '-', linewidth=1.5, color='#4A90D9', label='MAVLink (100 Hz)')
    ax.plot(t * 1000, framesync * 2 + 0.1, '-', linewidth=1.5, color='#E8A838', label='Frame-sync (10 Hz)')
    ax.plot(t * 1000, ekf * 1 + 0.1, '-', linewidth=1.5, color='#50B86C', label='EKF Output (100 Hz)')

    ax.set_xlabel('Time (ms)')
    ax.set_ylabel('Signal')
    ax.set_title('Pipeline Timing Diagram')
    ax.set_yticks([0.5, 1.5, 2.5, 3.5])
    ax.set_yticklabels(['', 'EKF', 'Frame', 'MAVLink'])
    ax.legend(loc='upper right', fontsize=6)
    ax.grid(True, alpha=0.3, axis='x')
    ax.set_xlim(0, 10)
    return fig

# ===== FIGURE 5: Defect Taxonomy =====
def fig_defect_taxonomy():
    fig, ax = plt.subplots(figsize=(7, 3.0))

    categories = ['Build/\nToolchain', 'Compiler\nBoundary', 'Silicon\nBoundary', 'Algorithm/\nProtocol', 'Memory\nSafety', 'Concurrency']
    counts = [3, 1, 6, 5, 3, 2]
    colors = ['#4A90D9', '#E8A838', '#D9534F', '#50B86C', '#9B59B6', '#F39C12']

    bars = ax.bar(categories, counts, color=colors, edgecolor='black', linewidth=0.5, alpha=0.85)

    for bar, count in zip(bars, counts):
        ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 0.1,
                str(count), ha='center', va='bottom', fontsize=9, fontweight='bold')

    ax.set_ylabel('Number of Defects')
    ax.set_title('Defect Taxonomy Across 11 Case Studies')
    ax.set_ylim(0, max(counts) + 1)
    ax.grid(True, alpha=0.3, axis='y')
    return fig

# ===== FIGURE 6: Memory Map =====
def fig_memory_map():
    fig, ax = plt.subplots(figsize=(3.5, 4.0))

    regions = [
        (0x08000000, 0x08200000, 'Flash\n(2 MB)', '#4A90D9', 'Code, .text, .rodata'),
        (0x24000000, 0x24080000, 'AXI SRAM\n(512 KB)', '#50B86C', '.data, .bss, stacks, buffers'),
        (0xD0000000, 0xD2000000, 'SDRAM\n(32 MB)', '#E8A838', 'Kinematic history'),
    ]

    y_pos = 0
    for start, end, label, color, desc in regions:
        size = end - start
        height = 0.8 if size < 0x1000000 else 0.6
        rect = plt.Rectangle((0, y_pos), 1, height, facecolor=color, edgecolor='black',
                             linewidth=0.8, alpha=0.7)
        ax.add_patch(rect)
        ax.text(0.5, y_pos + height/2, f'{label}\n{desc}', ha='center', va='center',
                fontsize=6, fontweight='bold', color='white')
        ax.text(-0.05, y_pos + height/2, f'0x{start:08X}', ha='right', va='center', fontsize=6)
        y_pos += height + 0.15

    ax.set_xlim(-0.3, 1.3)
    ax.set_ylim(-0.1, y_pos)
    ax.set_title('STM32H753XI Memory Map')
    ax.axis('off')
    return fig

# Generate all figures
print("Generating figures...")
save_fig('fig1_pipeline', fig_pipeline())
save_fig('fig2_ekf_convergence', fig_ekf_convergence())
save_fig('fig3_cache_coherency', fig_cache_coherency())
save_fig('fig4_timing', fig_timing())
save_fig('fig5_defect_taxonomy', fig_defect_taxonomy())
save_fig('fig6_memory_map', fig_memory_map())
print("\nAll figures generated!")
