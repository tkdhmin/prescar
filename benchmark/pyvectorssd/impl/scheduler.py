from .workload_base import WorkloadOperation, OperationType
from typing import List
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches
from .segment_tracker import SegmentTracker

OP_COLORS = {
    OperationType.VECTOR_INSERT:     "#1D9E75",
    OperationType.VECTOR_SEARCH:     "#378ADD",
    OperationType.INDEX_BUILD:       "#EF9F27",
    OperationType.INDEX_BUILD_ASYNC: "#EF9F27",
    OperationType.INDEX_BUILD_WAIT:  "#FAC775",
    OperationType.PAUSE_BUILD:       "#E24B4A",
    OperationType.RESUME_BUILD:      "#639922",
}

class PrescarScheduler:
    def __init__(self, tracker: SegmentTracker, policy: str, build_time: float = 37.8, build_deadline_margin: float = 0.3, t_indexed: float = 0.00319, t_unindexed: float = 0.00533, t_insert: float = 0.00000277, t_checkpoint: float = 0.06, t_resume: float = 0.028):
        """The default parameter values are collected from offline profiling"""
        tracker.status()
        self.tracker = tracker
        self.policy = policy
        self.build_time = build_time * tracker.n_unindexed
        self.build_deadline = build_time * (1 + build_deadline_margin)
        self.t_indexed = t_indexed
        self.t_unindexed = t_unindexed
        self.t_insert = t_insert
        self.t_checkpoint = t_checkpoint
        self.t_resume = t_resume

    def schedule(self, operations: List[WorkloadOperation]) -> List[WorkloadOperation]:
        if self.policy == "NP":
            return self._schedule_np(operations)
        elif self.policy == "SP-NP":
            return self._schedule_sp_np(operations)
        elif self.policy == "SP-P":
            return self._schedule_sp_p(operations)
        elif self.policy == "SLO-P":
            return self._schedule_slo_p(operations)
        else:
            raise ValueError(f"Unknown policy: {self.policy}")

    def _schedule_np(self, operations):
        return operations

    def _schedule_sp_np(self, operations):
        result = []
        pending_build = None
        for op in operations:
            if op.op_type == OperationType.INDEX_BUILD:
                pending_build = op
            elif pending_build and op.op_type == OperationType.VECTOR_SEARCH:
                result.append(op)
            else:
                if pending_build:
                    result.append(pending_build)
                    pending_build = None
                result.append(op)
        if pending_build:
            result.append(pending_build)
        return result

    def _schedule_sp_p(self, operations):
        result = []
        build_idx = None

        for i, op in enumerate(operations):
            if op.op_type == OperationType.INDEX_BUILD:
                build_idx = i
                break

        if build_idx is None:
            return operations

        for op in operations[:build_idx]:
            result.append(op)
            if op.op_type == OperationType.VECTOR_INSERT:
                self.tracker.on_insert()

        post_ops = operations[build_idx + 1:]
        searches = [op for op in post_ops if op.op_type == OperationType.VECTOR_SEARCH]
        inserts = [op for op in post_ops if op.op_type == OperationType.VECTOR_INSERT]

        result.append(WorkloadOperation(OperationType.INDEX_BUILD_ASYNC, operations[build_idx].arrival_time))
        if searches:
            result.append(WorkloadOperation(OperationType.PAUSE_BUILD, operations[build_idx].arrival_time))
            result.extend(searches)
            result.append(WorkloadOperation(OperationType.RESUME_BUILD, searches[-1].arrival_time))
        result.extend(inserts)
        result.append(WorkloadOperation(OperationType.INDEX_BUILD_WAIT, inserts[-1].arrival_time))
        return result

    def _schedule_slo_p(self, operations):
        result = []
        build_idx = None

        for i, op in enumerate(operations):
            if op.op_type == OperationType.INDEX_BUILD:
                build_idx = i
                break

        if build_idx is None:
            return operations

        for op in operations[:build_idx]:
            result.append(op)
            if op.op_type == OperationType.VECTOR_INSERT:
                self.tracker.on_insert()

        tsearch = self.tracker.tsearch_total(self.t_indexed, self.t_unindexed)

        admitted = []
        rejected_searches = []
        n_admitted_search = 0
        n_proceding_insert = 0

        post_ops = operations[build_idx + 1:]
        for op in post_ops:
            if op.op_type == OperationType.VECTOR_INSERT:
                admitted.append(op)
                n_proceding_insert += 1
            elif op.op_type == OperationType.VECTOR_SEARCH:
                remaining = (self.build_deadline - self.build_time - self.t_checkpoint
                - self.t_resume - n_proceding_insert * self.t_insert)
                remaining -= (self.t_resume + self.t_checkpoint)
                n_max = int(remaining / (tsearch)) if tsearch > 0 else 0
                n_max = max(n_max, 0)

                eq2 = (op.deadline is not None and self.t_checkpoint + tsearch + self.t_resume <= op.deadline)
                
                if eq2 and n_admitted_search < n_max:
                    admitted.append(op)
                    n_admitted_search += 1
                else:
                    rejected_searches.append(op)

        result.append(WorkloadOperation(OperationType.INDEX_BUILD_ASYNC, operations[build_idx].arrival_time))
        if admitted:
            result.append(WorkloadOperation(OperationType.PAUSE_BUILD, operations[build_idx].arrival_time))
            result.extend(admitted)
            result.append(WorkloadOperation(OperationType.RESUME_BUILD, admitted[-1].arrival_time))
        result.extend(rejected_searches)
        result.append(WorkloadOperation(OperationType.INDEX_BUILD_WAIT, admitted[-1].arrival_time + 1e-6))
        return result


    def save_schedule_png(self, scheduled_ops_per_policy: dict, output_path: str = "schedule.png") -> None:
        policies = list(scheduled_ops_per_policy.keys())
        n_policies = len(policies)

        fig, axes = plt.subplots(n_policies, 1, figsize=(20, n_policies * 2.5))
        if n_policies == 1:
            axes = [axes]

        for ax, policy in zip(axes, policies):
            ops = scheduled_ops_per_policy[policy]

            groups = []
            for op in ops:
                if groups and groups[-1][0] == op.op_type:
                    groups[-1][1] += 1
                else:
                    groups.append([op.op_type, 1])

            ax.set_xlim(0, len(groups))
            ax.set_ylim(0, 1)
            ax.set_yticks([])
            ax.set_xticks([])
            ax.set_ylabel(policy, fontsize=11, rotation=0, labelpad=60, va='center')

            pause_idx = [i for i, g in enumerate(groups) if g[0] == OperationType.PAUSE_BUILD]
            resume_idx = [i for i, g in enumerate(groups) if g[0] == OperationType.RESUME_BUILD]
            for p, r in zip(pause_idx, resume_idx):
                ax.axvspan(p, r + 1, alpha=0.08, color="#E24B4A")

            for i, (op_type, count) in enumerate(groups):
                color = OP_COLORS.get(op_type, "#888780")
                rect = mpatches.FancyBboxPatch(
                    (i + 0.05, 0.1), 0.9, 0.8,
                    boxstyle="round,pad=0.02",
                    facecolor=color, edgecolor="white", linewidth=0.5
                )
                ax.add_patch(rect)
                label = op_type.value.replace("_", "\n")
                if count > 1:
                    label += f"\n×{count}"
                ax.text(i + 0.5, 0.5, label, ha='center', va='center',
                        fontsize=7, color="white", fontweight='bold')

        legend_patches = [mpatches.Patch(color=c, label=t) for t, c in [
            ("INSERT",      "#1D9E75"),
            ("SEARCH",      "#378ADD"),
            ("BUILD/ASYNC", "#EF9F27"),
            ("BUILD_WAIT",  "#FAC775"),
            ("PAUSE",       "#E24B4A"),
            ("RESUME",      "#639922"),
        ]]
        fig.legend(handles=legend_patches, loc='lower center', ncol=6,
                fontsize=8, bbox_to_anchor=(0.5, -0.02))

        plt.suptitle("Scheduler Policy — Operation Sequence", fontsize=13, y=1.01)
        plt.tight_layout()
        plt.savefig(output_path, dpi=150, bbox_inches='tight')
        plt.close()
        print(f"Saved: {output_path}")