MAX_SKIPLIST_ON_SSD: int = 4096
MAX_GROWING_SEGMENT_NUM: int = 3

class SegmentTracker:
    def __init__(self):
        self.insert_count = 0
        self.growing_count = 0
        self.n_indexed = 0
        self.n_unindexed = 0
    
    def status(self):
        print(f"# Insert: {self.insert_count}, # of GrowingSeg: {self.growing_count}, N_indexed: {self.n_indexed}, N_unindexed: {self.n_unindexed}.")

    def on_insert(self):
        self.insert_count += 1
        if self.insert_count % MAX_SKIPLIST_ON_SSD == 0:
            self.growing_count += 1
        if self.growing_count == MAX_GROWING_SEGMENT_NUM:
            self.n_unindexed += 1
            self.growing_count = 0

    def on_build_complete(self):
        self.n_indexed += self.n_unindexed
        self.n_unindexed = 0

    def tsearch_total(self, t_indexed: float, t_unindexed: float) -> float:
        return self.n_indexed * t_indexed + self.n_unindexed * t_unindexed

    def copy(self) -> "SegmentTracker":
        t = SegmentTracker()
        t.insert_count = self.insert_count
        t.growing_count = self.growing_count
        t.n_indexed = self.n_indexed
        t.n_unindexed = self.n_unindexed
        return t