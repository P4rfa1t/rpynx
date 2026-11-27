"""Minimal setuptools stub for running Ren'Py 7.8.7 and pygame_sdl2's generate
step with host Python 2.7.18 without installing setuptools."""


class SetuptoolsDeprecationWarning(DeprecationWarning):
    pass


class Extension(object):
    def __init__(self, *args, **kwargs):
        pass


def setup(*args, **kwargs):
    pass
