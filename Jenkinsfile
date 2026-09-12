// Minimal Jenkinsfile: mirror-only, no build/test/image stages yet.
// This repo has no Dockerfile and no compiled code worth CI-building yet
// (server/client are scaffold placeholders) — those stages get added once
// there's real code and a real image to build. See templates/PROJECT_CI_SETUP.md
// for how the Jenkins job itself is wired (Gitea webhook, Multibranch).
//
// "Mirror to GitHub": same SSH-deploy-key pattern as sovereign-ai-nexus (see
// that repo's Jenkinsfile and PROJECT_CI_SETUP.md's "SSH-key-based mirror"
// section) — one deploy key, write-scoped to this GitHub repo only via a
// GitHub Deploy Key, not a broader PAT. Runs right after checkout so mirroring
// never depends on a build that doesn't exist yet.

pipeline {
    // Docker-CI migration (Homelab Redux Valul 1): Kubernetes/Kaniko agent
    // retired along with the K3s cluster. This stage only checks out and
    // mirrors — no image build, no daemon access needed — so it just runs
    // on the Jenkins controller directly.
    agent any
    environment {
        GITHUB_MIRROR_URL = 'git@github.com:Petrickah/cpp-socket-chat.git'
    }
    stages {
        stage('Checkout') {
            steps {
                checkout scm
            }
        }
        stage('Mirror to GitHub') {
            steps {
                withCredentials([sshUserPrivateKey(credentialsId: 'github-mirror-cpp-socket-chat', keyFileVariable: 'SSH_KEY')]) {
                    sh '''
                    export GIT_SSH_COMMAND="ssh -i $SSH_KEY -o StrictHostKeyChecking=accept-new -o UserKnownHostsFile=/dev/null"
                    git push "${GITHUB_MIRROR_URL}" "HEAD:refs/heads/${BRANCH_NAME}"
                    '''
                }
            }
        }
    }
}
