from server.data.passphrases import PASSPHRASES
from server.data.questions import QUESTIONS_STANDARD, QUESTIONS_INTERESTING


class TestPassphrases:
    def test_minimum_count(self):
        assert len(PASSPHRASES) >= 100

    def test_all_strings(self):
        assert all(isinstance(p, str) for p in PASSPHRASES)

    def test_no_duplicates(self):
        assert len(PASSPHRASES) == len(set(PASSPHRASES))

    def test_contain_hyphen(self):
        for p in PASSPHRASES:
            assert "-" in p, f"no hyphen: {p!r}"

    def test_no_spaces(self):
        for p in PASSPHRASES:
            assert " " not in p, f"has space: {p!r}"

    def test_lowercase(self):
        for p in PASSPHRASES:
            assert p == p.lower(), f"not lowercase: {p!r}"

    def test_ascii_only(self):
        for p in PASSPHRASES:
            p.encode("ascii")


class TestQuestions:
    def test_standard_count(self):
        assert len(QUESTIONS_STANDARD) >= 8

    def test_interesting_count(self):
        assert len(QUESTIONS_INTERESTING) >= 8

    def test_no_duplicates_standard(self):
        assert len(QUESTIONS_STANDARD) == len(set(QUESTIONS_STANDARD))

    def test_no_duplicates_interesting(self):
        assert len(QUESTIONS_INTERESTING) == len(set(QUESTIONS_INTERESTING))

    def test_no_overlap(self):
        assert not set(QUESTIONS_STANDARD) & set(QUESTIONS_INTERESTING)

    def test_end_with_question_mark(self):
        all_q = QUESTIONS_STANDARD + QUESTIONS_INTERESTING
        for q in all_q:
            assert q.rstrip().endswith("?"), f"not a question: {q!r}"
