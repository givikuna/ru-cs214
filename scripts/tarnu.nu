#!/usr/bin/env nu

def main [project?: string] {
    let current_dir: oneof<string, list<string>> = ($env.PWD | path basename)

    let target: any = if ($project != null) {
        $project
    } else if ($current_dir | str starts-with "P") {
        $current_dir
    } else {
        print "error: Please specify a project (e.g., tarnu P2) or run from inside a project directory."
        return
    }

    let tar_name: string = $"($target | str lowercase).tar"
    let tar_path: string = $"tars/($tar_name)"

    if $current_dir == $target {
        print $"cleaning ($target)..."
        ^make clean
        cd ..
    } else if ($target | path exists) {
        cd $target
        print $"cleaning ($target)..."
        ^make clean
        cd ..
    } else {
        print $"error: could not find directory ($target)."
        return
    }

    if not ("tars" | path exists) {
        mkdir tars
    }

    print $"creating ($tar_path)..."
    ^tar -vzcf $tar_path $target

    print $"success! ($tar_path) is ready."
}
