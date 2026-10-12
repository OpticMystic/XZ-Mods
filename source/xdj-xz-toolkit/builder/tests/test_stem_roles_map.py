import unittest

from builder import stem_roles_map as roles

SUNO_12 = ('Vocals', 'Backing Vocals', 'Drums', 'Bass', 'Guitar', 'Keys', 'Strings', 'Brass', 'Woodwinds',
           'Percussion', 'Synth', 'FX')
SUNO_GROUPS = {'Vocals': 'vocals', 'Backing Vocals': 'vocals', 'Drums': 'drums', 'Percussion': 'drums',
               'Bass': 'harmonics', 'Guitar': 'harmonics', 'Keys': 'harmonics', 'Keyboard': 'harmonics',
               'Strings': 'harmonics', 'Brass': 'harmonics', 'Woodwinds': 'harmonics', 'Synth': 'harmonics',
               'FX': 'harmonics'}


def groups(result):
    return [entry['group'] for entry in result['files']]


class SunoTests(unittest.TestCase):
    def test_twelve_stems_map_automatically(self):
        for pattern in ('{}.wav', 'Midnight Drive ({}).mp3', 'Midnight Drive - {}.wav', '07 {}.wav'):
            names = [pattern.format(name) for name in SUNO_12]
            result = roles.map_files(names, titles=['Midnight Drive'])
            self.assertEqual(groups(result), [SUNO_GROUPS[name] for name in SUNO_12], pattern)
            self.assertEqual(result['verdict'], 'auto', result['warnings'])
            self.assertEqual(result['source'], 'suno')
            self.assertEqual(result['layout'], 'groups')

    def test_keyboard_spelling_and_full_song_in_the_zip(self):
        stems = [name.replace('Keys', 'Keyboard') for name in SUNO_12]
        names = [f'Midnight Drive ({name}).wav' for name in stems] + ['Midnight Drive.wav']
        result = roles.map_files(names, titles=['Midnight Drive'])
        self.assertEqual(groups(result)[:-1], [SUNO_GROUPS[name] for name in stems])
        self.assertEqual(result['files'][-1]['group'], roles.MIX)
        self.assertEqual(result['verdict'], 'auto')

    def test_two_stem_set_needs_a_drum_split(self):
        result = roles.map_files(['Midnight Drive (Vocals).wav', 'Midnight Drive (Instrumental).wav'],
                                 hints=['Midnight Drive Suno Stems'])
        self.assertEqual(groups(result), ['vocals', roles.INSTRUMENTAL])
        self.assertEqual(result['layout'], 'vocals_instrumental')
        self.assertEqual(result['source'], 'suno')
        self.assertEqual(result['verdict'], 'auto')


class OtherToolTests(unittest.TestCase):
    def test_demucs_four_and_six_stems(self):
        four = roles.map_files(['vocals.wav', 'drums.wav', 'bass.wav', 'other.wav'])
        self.assertEqual(groups(four), ['vocals', 'drums', 'harmonics', 'harmonics'])
        self.assertEqual((four['verdict'], four['source']), ('auto', 'generic'))
        six = roles.map_files(['vocals.wav', 'drums.wav', 'bass.wav', 'other.wav', 'guitar.wav', 'piano.wav'])
        self.assertEqual(groups(six)[-2:], ['harmonics', 'harmonics'])

    def test_uvr_suffixes_ignore_the_model_name(self):
        result = roles.map_files(['1_Midnight Drive_(Vocals)_UVR-MDX-NET-Inst_HQ_3.wav',
                                  '1_Midnight Drive_(Instrumental)_UVR-MDX-NET-Inst_HQ_3.wav'])
        self.assertEqual(groups(result), ['vocals', roles.INSTRUMENTAL])
        self.assertEqual(roles.classify('midnight_drive_vocals_UVR_MDXNET_Inst_HQ.wav')['group'], 'vocals')

    def test_instrumental_synonyms(self):
        for name in ('Song (No Vocals).wav', 'accompaniment.wav', 'song_novocals.flac', 'Song - Karaoke.mp3',
                     'Song [Instrumental].wav', 'song-without-vocals.wav'):
            self.assertEqual(roles.classify(name)['group'], roles.INSTRUMENTAL, name)

    def test_case_separators_numbers_and_camel_case(self):
        self.assertEqual(roles.classify('03-LEAD_VOCALS.wav')['label'], 'Vocals')
        self.assertEqual(roles.classify('BackingVocals.wav')['label'], 'Backing Vocals')
        self.assertEqual(roles.classify('HiHat_02.wav')['group'], 'drums')
        self.assertEqual(roles.classify('song_vocals.wav')['group'], 'vocals')

    def test_mix_names_and_the_tracks_own_file_are_excluded(self):
        for name in ('Full Mix.wav', 'original.wav', 'Song (Master).wav', 'mixdown.flac', 'Song (Original Mix).mp3'):
            self.assertEqual(roles.classify(name)['group'], roles.MIX, name)
        self.assertEqual(roles.classify('Bass Drop.wav', titles=['Bass Drop'])['group'], roles.MIX)
        self.assertEqual(roles.classify('01 Bass Drop.wav', titles=['Bass Drop'])['group'], roles.MIX)
        self.assertEqual(roles.classify('Bass Drop - Vocals.wav', titles=['Bass Drop'])['group'], 'vocals')

    def test_last_bracket_wins_over_title_words(self):
        self.assertEqual(roles.classify('Bass Drop (Extended Mix) (Drums).wav')['group'], 'drums')


class ReviewTests(unittest.TestCase):
    def test_unknown_names_need_review(self):
        result = roles.map_files(['vocals.wav', 'track 4.wav'])
        self.assertEqual(result['files'][1]['group'], None)
        self.assertEqual(result['verdict'], 'needs_review')
        self.assertIn('track 4.wav', result['warnings'][0])

    def test_conflicting_title_words_lower_confidence(self):
        entry = roles.classify('Bass Drop_drums.wav')
        self.assertEqual(entry['group'], 'drums')
        self.assertLess(entry['confidence'], roles.AUTO_CONFIDENCE)

    def test_instrumental_with_separate_parts_is_double_counting(self):
        result = roles.map_files(['vocals.wav', 'instrumental.wav', 'drums.wav'])
        self.assertEqual(result['verdict'], 'needs_review')
        self.assertTrue(any('already contains' in warning for warning in result['warnings']))

    def test_duplicate_parts_and_empty_sets_need_review(self):
        self.assertEqual(roles.map_files(['a/drums.wav', 'b/drums.wav'])['verdict'], 'needs_review')
        self.assertEqual(roles.map_files(['Full Mix.wav'])['verdict'], 'needs_review')

    def test_overrides_win(self):
        result = roles.map_files(['track 4.wav', 'drums.wav'], overrides=['harmonics', 'mix'])
        self.assertEqual(groups(result), ['harmonics', 'mix'])
        self.assertEqual([entry['origin'] for entry in result['files']], ['override', 'override'])
        self.assertEqual(result['verdict'], 'auto')
        with self.assertRaises(ValueError):
            roles.map_files(['drums.wav'], overrides=[''])
        with self.assertRaises(ValueError):
            roles.map_files(['drums.wav'], overrides=['drums', 'vocals'])


if __name__ == '__main__':
    unittest.main()
