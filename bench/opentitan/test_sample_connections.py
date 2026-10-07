import unittest

from sample_connections import evaluate_annotations, select_sample


class SampleConnectionsTests(unittest.TestCase):
    def test_selection_is_order_independent_and_stratified(self):
        rows = []
        for kind in ("direct", "approximate"):
            for ranged in (False, True):
                for index in range(3):
                    row = {"source": f"src_{kind}_{ranged}_{index}", "dest": "sink", "kind": kind}
                    if ranged:
                        row.update(source_bits={"low": index, "high": index},
                                   dest_bits={"low": 0, "high": 0})
                    rows.append(row)
        first = select_sample({"connections": rows}, "seed", 2)
        reverse = select_sample({"connections": list(reversed(rows))}, "seed", 2)
        self.assertEqual(first, reverse)
        self.assertEqual(len(first["samples"]), 8)
        self.assertTrue(all(count == 3 for count in first["population"].values()))
        with self.assertRaisesRegex(ValueError, "seed"):
            select_sample({"connections": rows}, "", 2)
        with self.assertRaisesRegex(ValueError, "per_stratum"):
            select_sample({"connections": rows}, "seed", 0)

    def test_annotations_require_exact_ids_and_explicit_uncertainty(self):
        rows = [{"source": str(index), "dest": "sink", "kind": kind,
                 **({"source_bits": {"low": 0, "high": 0},
                     "dest_bits": {"low": 0, "high": 0}} if ranged else {})}
                for index, (kind, ranged) in enumerate(
                    (("direct", False), ("direct", True),
                     ("approximate", False), ("approximate", True)))]
        sample = select_sample({"connections": rows}, "seed", 1)
        labels = [{"id": row["id"], "verdict": "confirmed", "evidence": "rtl.sv:1"}
                  for row in sample["samples"]]
        annotations = {"seed": "seed", "population_sha256": sample["population_sha256"],
                       "labels": labels}
        self.assertEqual(evaluate_annotations(sample, annotations)["overall"]["confirmed"], 4)
        labels[0] = {"id": labels[0]["id"], "verdict": "unresolved", "note": "needs trace"}
        self.assertEqual(evaluate_annotations(sample, annotations)["overall"]["unresolved"], 1)
        with self.assertRaisesRegex(ValueError, "stale"):
            evaluate_annotations(sample, {**annotations, "population_sha256": "old"})
        with self.assertRaisesRegex(ValueError, "source evidence"):
            evaluate_annotations(sample, {**annotations, "labels": [{**labels[0], "verdict": "confirmed"},
                                                                    *labels[1:]]})


if __name__ == "__main__":
    unittest.main()
